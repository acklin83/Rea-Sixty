#include "Phones.h"

#include "JsonTree.h"
#include "RmeNames.h"
#include "RmeStrip.h"
#include "RmeUf1.h"
#include "Uf1EqCurve.h"
#include "Sha256.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

namespace orc::phones {

namespace rme = reasixty::rme;
namespace rmeu = reasixty::rme::uf1;
namespace strip = reasixty::rme::strip;

// ── the PIN ──────────────────────────────────────────────────────────────────

PinChecker::PinChecker(const std::string& pin)
{
    required_ = !pin.empty();
    if (required_) reasixty::sha256(pin.data(), pin.size(), want_);
}

bool PinChecker::ok(const std::string& given) const
{
    if (!required_) return true;
    unsigned char got[32];
    reasixty::sha256(given.data(), given.size(), got);
    // Fixed-length digests and an OR over every byte: no early exit.
    unsigned char diff = 0;
    for (int i = 0; i < 32; ++i) diff |= static_cast<unsigned char>(got[i] ^ want_[i]);
    return diff == 0;
}

long long lockLeft(const FailRecord* rec, long long nowMs)
{
    return rec && rec->lockedUntil > nowMs ? rec->lockedUntil - nowMs : 0;
}

FailRecord afterFail(const FailRecord* rec, long long nowMs)
{
    const bool fresh = !rec || nowMs - rec->since > kWindowMs;
    FailRecord r = fresh ? FailRecord{ 0, nowMs, 0 } : *rec;
    r.fails += 1;
    if (r.fails >= kMaxFails) { r.lockedUntil = nowMs + kLockMs; r.fails = 0; r.since = nowMs; }
    return r;
}

// pin.js: /^(127\.|10\.|192\.168\.|172\.(1[6-9]|2\d|3[01])\.|100\.(6[4-9]|[7-9]\d|1[01]\d|12[0-7])\.)/
// or ::1. Prefix rules, so the first two octets decide.
bool isPrivate(const std::string& ip)
{
    if (ip == "::1") return true;
    auto starts = [&](const char* p) { return ip.rfind(p, 0) == 0; };
    if (starts("127.") || starts("10.") || starts("192.168.")) return true;
    auto second = [&](const char* first, int lo, int hi) {
        const std::size_t n = std::strlen(first);
        if (!starts(first)) return false;
        std::size_t i = n, digits = 0;
        int v = 0;
        while (i < ip.size() && ip[i] >= '0' && ip[i] <= '9' && digits < 3) {
            v = v * 10 + (ip[i] - '0');
            ++i; ++digits;
        }
        // The regex wants exactly the octet and then a dot (no leading zeros
        // beyond what its alternatives spell, which they never do).
        if (digits == 0 || i >= ip.size() || ip[i] != '.') return false;
        if (digits > 1 && ip[n] == '0') return false;
        return v >= lo && v <= hi;
    };
    return second("172.", 16, 31) || second("100.", 64, 127);
}

std::string clientKey(const std::string& peer, const std::string& xRealIp,
                      const std::string& xForwardedFor)
{
    auto trim = [](std::string t) {
        while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
        std::size_t b = 0;
        while (b < t.size() && (t[b] == ' ' || t[b] == '\t')) ++b;
        return t.substr(b);
    };
    std::string p = peer;
    if (p.rfind("::ffff:", 0) == 0) p = p.substr(7);
    if (!isPrivate(p)) return p;
    std::string real = trim(xRealIp);
    if (real.empty()) {
        const std::string xff = xForwardedFor;
        const auto comma = xff.rfind(',');
        real = trim(comma == std::string::npos ? xff : xff.substr(comma + 1));
    }
    return real.empty() ? p : real;
}

// ── colours ──────────────────────────────────────────────────────────────────

std::string colourHex(int index)
{
    // bridge.js TMFX_COLORS; 1 (white) is TotalMix' unset state.
    switch (index) {
        case 2: return "#9e9e9e";   // grey
        case 3: return "#ff8c00";   // orange
        case 4: return "#e53935";   // red
        case 5: return "#2196f3";   // blue
        case 6: return "#43a047";   // green
        case 7: return "#ffd400";   // yellow
        case 8: return "#e91e63";   // pink
        default: return {};
    }
}

namespace {

// bridge.js PALETTE: what a channel without its own colour gets, by position.
const char* const kPalette[8] = { "#0099FF", "#3fcf6a", "#f0c000", "#a000a0",
                                  "#ff6b35", "#00c2c2", "#e0457b", "#7a5cff" };

// Fader ceiling: inputs +6 dB, playbacks 0 dB (bridge.js maxOf).
int maxOf(const std::string& kind) { return kind == "in" ? 6 : 0; }

double clampDb(double v, double max)
{
    if (!std::isfinite(v)) return -300.0;
    return std::clamp(v, -300.0, max);
}
double clampPan(double v)
{
    if (!std::isfinite(v)) return 0.0;
    return std::clamp(v, -1.0, 1.0);
}
// The value TotalMix gets is a float; the mirror holds what it got, so a value
// the phones set and the same value reported back compare equal.
double asSent(double v) { return static_cast<double>(static_cast<float>(v)); }

// ── a JSON writer, just enough for the messages below ───────────────────────

std::string jstr(const std::string& raw)
{
    const std::string s = rme::toUtf8(raw);
    std::string o = "\"";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '"')       o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c < 0x20) {
            char b[8];
            snprintf(b, sizeof(b), "\\u%04x", c);
            o += b;
        } else o.push_back(ch);
    }
    return o + "\"";
}

std::string jnum(double v)
{
    if (!std::isfinite(v)) return "null";   // what JSON.stringify writes
    char b[32];
    snprintf(b, sizeof(b), "%.9g", v);
    return b;
}

// "phone3" -> 3, anything else -> 0 (bridge.js phoneOf).
int phoneOf(const std::string& id)
{
    if (id.size() < 6 || id.compare(0, 5, "phone") != 0) return 0;
    for (std::size_t i = 5; i < id.size(); ++i)
        if (id[i] < '0' || id[i] > '9') return 0;
    if (id.size() - 5 > 6) return 0;
    return std::atoi(id.c_str() + 5);
}

// "in:3" -> kind "in", n 3. False for anything else.
bool splitSrc(const std::string& src, std::string& kind, int& n)
{
    const std::size_t c = src.find(':');
    if (c == std::string::npos) return false;
    kind = src.substr(0, c);
    if (kind != "in" && kind != "pb") return false;
    if (c + 1 >= src.size() || src.size() - c - 1 > 6) return false;
    for (std::size_t i = c + 1; i < src.size(); ++i)
        if (src[i] < '0' || src[i] > '9') return false;
    n = std::atoi(src.c_str() + c + 1);
    return true;
}

rme::Bus busOfKind(const std::string& kind)
{
    return kind == "in" ? rme::Bus::Input : rme::Bus::Playback;
}

std::string memoKey(int bus, const std::string& src)
{
    return std::to_string(bus) + ":" + src;
}

// One field of a parsed message. JSON's value, as JavaScript would read it.
struct Msg {
    std::string t, pin, mix, src, ctrl, performer, target, param, group;
    bool        pinIsString = false;
    bool        force       = false;
    bool        on          = true;   // "meters
    double      val         = NAN;
};

bool parseMsg(const std::string& json, Msg& out)
{
    wdl_json_parser p;
    const wdl_json_element* root = p.parse(json.c_str(), static_cast<int>(json.size()));
    JsonTreeGuard rootGuard{p, root};
    if (!root || !root->is_object()) return false;
    auto text = [&](const char* k, std::string& dst, bool* wasString = nullptr) {
        const wdl_json_element* e = root->get_item_by_name(k);
        if (!e || e->m_array || !e->m_value) return;
        if (wasString) *wasString = e->m_value_string;
        // String(m.src) of a number is its digits; the raw value is that.
        dst = e->m_value;
    };
    text("t", out.t);
    text("pin", out.pin, &out.pinIsString);
    text("mix", out.mix);
    text("src", out.src);
    text("ctrl", out.ctrl);
    text("performer", out.performer);
    text("target", out.target);
    text("param", out.param);
    text("group", out.group);
    if (const wdl_json_element* f = root->get_item_by_name("force"); f && f->m_value) {
        // JavaScript truthiness of what the page can send (true, or a number).
        const std::string v = f->m_value;
        out.force = f->m_value_string ? !v.empty()
                  : !(v == "false" || v == "null" || v == "0" || v == "0.0");
    }
    if (const wdl_json_element* f = root->get_item_by_name("on"); f && f->m_value) {
        const std::string v = f->m_value;
        out.on = f->m_value_string ? !v.empty()
               : !(v == "false" || v == "null" || v == "0" || v == "0.0");
    }
    if (const wdl_json_element* v = root->get_item_by_name("val");
        v && v->m_value && !v->m_value_string) {
        char* end = nullptr;
        const double d = std::strtod(v->m_value, &end);
        if (end && *end == '\0') out.val = d;
    }
    return true;
}

} // namespace

// ── the mixer ────────────────────────────────────────────────────────────────

Mixer::Mixer(const std::string& pin) : pin_(pin) {}

void Mixer::setPin(const std::string& pin)
{
    const PinChecker next(pin);
    pin_ = next;
    fails_.clear();
    for (auto& [id, c] : clients_) {
        c.mix.clear();
        c.cfgJson.clear();
        c.authed = !pin_.required();
        if (!c.authed) send_(id, R"({"t":"pin","wrong":0,"wait":0})");
    }
}

int Mixer::open(const std::string& key)
{
    const int id = nextId_++;
    Client c;
    c.key    = key;
    c.authed = !pin_.required();
    clients_[id] = c;
    return id;
}

void Mixer::close(int id)
{
    const auto it = clients_.find(id);
    if (it == clients_.end()) return;
    const bool had = !it->second.mix.empty();
    clients_.erase(it);
    if (had) pushConfig_();   // the claim is free at once
}

int Mixer::busOfPhone_(int phone) const
{
    if (phone < 1 || phone > 4) return -1;
    return st_.phones[phone - 1];   // -1 = no output assigned
}

const Performer* Mixer::perfOfMix_(const std::string& mix) const
{
    if (mix.rfind("perf:", 0) != 0) return nullptr;
    const std::string tok = mix.substr(5);
    for (const Performer& p : perf_)
        if (p.id == tok) return &p;
    return nullptr;
}

// -1: no such mix, or its output has no channel right now. Phones 1-4 only while
// there are no performers.
int Mixer::busOfMix_(const std::string& mix) const
{
    if (const Performer* p = perfOfMix_(mix)) return rmeu::keyOutput(st_, p->output);
    return perf_.empty() ? busOfPhone_(phoneOf(mix)) : -1;
}

void Mixer::setPerformers(const std::vector<Performer>& v)
{
    perf_ = v;
    for (auto& [id, c] : clients_) {
        c.cfgJson.clear();
        if (!c.authed || c.mix.empty()) continue;
        if (busOfMix_(c.mix) < 0) { c.mix.clear(); c.stripTarget.clear(); c.srcSig.clear(); }
    }
    pushConfig_();
    resnapViewers_();
}

void Mixer::setGroups(const std::vector<Group>& v)
{
    if (v == groups_) return;
    groups_ = v;
    for (auto& [id, c] : clients_) c.groupSent.clear();
    resnapViewers_();
}

const Group* Mixer::groupOfSrc_(const std::string& src) const
{
    for (const Group& g : groups_)
        if (std::find(g.members.begin(), g.members.end(), src) != g.members.end()) return &g;
    return nullptr;
}

std::vector<Mixer::Src> Mixer::groupMembers_(const std::string& mix, const std::string& group) const
{
    std::vector<Src> out;
    for (const Src& x : sourcesOf_(mix))
        if (x.group == group) out.push_back(x);
    return out;
}

int Mixer::holderOf_(const std::string& mixId) const
{
    for (const auto& [id, c] : clients_)
        if (c.authed && c.mix == mixId) return id;
    return 0;
}

int Mixer::clients() const
{
    int n = 0;
    for (const auto& [id, c] : clients_) n += c.authed ? 1 : 0;
    return n;
}

int Mixer::held() const
{
    std::set<std::string> h;
    for (const auto& [id, c] : clients_)
        if (c.authed && !c.mix.empty()) h.insert(c.mix);
    return static_cast<int>(h.size());
}

void Mixer::send_(int id, const std::string& json) { outbox_.emplace_back(id, json); }

void Mixer::osc_(const std::string& address, float v)
{
    oscQ_.push_back({ address, v });
    // Into the mirror at once, as Manager::send folds it into its own state:
    // TotalMix does not echo to the remote that sent it.
    rme::ingest(st_, rme::localEcho(address, v));
}

std::vector<std::pair<int, std::string>> Mixer::takeOut()
{
    std::vector<std::pair<int, std::string>> o;
    o.swap(outbox_);
    return o;
}

std::vector<Osc> Mixer::takeOsc()
{
    std::vector<Osc> o;
    o.swap(oscQ_);
    return o;
}

bool Mixer::takeRefresh()
{
    const bool r = refresh_;
    refresh_ = false;
    return r;
}

// The level a source shows on this headphone: the remembered one while muted.
double Mixer::shownDb_(int bus, const std::string& kind, int n) const
{
    const auto m = mute_.find(memoKey(bus, kind + ":" + std::to_string(n)));
    if (m != mute_.end() && m->second.muted) return m->second.db;
    const double* v = st_.mixFader(busOfKind(kind), n, bus);
    return v ? *v : -300.0;
}

std::string Mixer::configJson_(int forId) const
{
    const auto self = clients_.find(forId);
    const std::string token = self == clients_.end() ? std::string() : self->second.token;
    std::string mixes = "[";
    bool first = true;
    // The output's colour and name, what a mix shows when nobody named it.
    auto outputLook = [&](int bus, std::string& label, std::string& colour) {
        if (const auto o = st_.outputs.find(bus); o != st_.outputs.end()) {
            if (!o->second.name.empty()) label = o->second.name;
            colour = colourHex(o->second.colour);
        }
    };
    std::string link = "ok";
    if (!token.empty() || !perf_.empty()) {
        // ⇨ A LINK SHOWS ITS PERFORMER AND NOBODY ELSE. Without one, the list.
        bool found = false, routed = false;
        for (std::size_t i = 0; i < perf_.size(); ++i) {
            const Performer& p = perf_[i];
            if (!token.empty() && p.id != token) continue;
            found = true;
            const std::string id = "perf:" + p.id;
            const int bus = busOfMix_(id);
            if (bus < 0) continue;
            routed = true;
            const std::string out = rmeu::displayName(st_, rmeu::Row::Output, bus);
            std::string colour = p.colour;
            if (colour.empty()) colour = kPalette[i % 8];
            if (!first) mixes += ",";
            first = false;
            mixes += "{\"id\":" + jstr(id) + ",\"bus\":" + std::to_string(bus)
                   + ",\"label\":" + jstr(p.name) + ",\"role\":" + jstr(p.role)
                   + ",\"out\":" + jstr(out) + ",\"color\":" + jstr(colour) + ",\"locked\":false}";
        }
        if (!token.empty()) link = !found ? "unknown" : !routed ? "noOutput" : "ok";
    } else {
        for (int p = 1; p <= 4; ++p) {
            const int bus = busOfPhone_(p);
            if (bus < 0) continue;
            const std::string id = "phone" + std::to_string(p);
            std::string label = "Phones " + std::to_string(p);
            std::string colour;
            outputLook(bus, label, colour);
            if (colour.empty()) colour = kPalette[(p - 1) % 8];
            const int h = holderOf_(id);
            if (!first) mixes += ",";
            first = false;
            mixes += "{\"id\":" + jstr(id) + ",\"phone\":" + std::to_string(p)
                   + ",\"bus\":" + std::to_string(bus) + ",\"label\":" + jstr(label)
                   + ",\"color\":" + jstr(colour)
                   + ",\"locked\":" + ((h != 0 && h != forId) ? "true" : "false") + "}";
        }
    }
    mixes += "]";
    return "{\"t\":\"config\",\"version\":" + jstr(version_) + ",\"tmSeen\":"
         + (tmSeen_ ? "true" : "false") + ",\"link\":" + jstr(link) + ",\"mixes\":" + mixes + "}";
}

// What a mix shows, in its order. Phones 1-4 and a performer on TotalMix' layout:
// the shared visible set, inputs then playbacks. A performer's own list: those of
// its faders TotalMix shows right now; a hidden channel's fader waits in the file.
std::vector<Mixer::Src> Mixer::sourcesOf_(const std::string& mix) const
{
    std::vector<Src> out;
    int i = 0;
    auto look = [&](const std::string& kind, int n) {
        Src x;
        x.kind = kind;
        x.n    = n;
        x.id   = kind + ":" + std::to_string(n);
        const auto& m = kind == "in" ? st_.inputs : st_.playbacks;
        if (const auto c = m.find(n); c != m.end()) {
            x.label  = rme::toUtf8(c->second.name);
            x.colour = colourHex(c->second.colour);
        }
        if (x.label.empty()) x.label = (kind == "in" ? "IN " : "PB ") + std::to_string(n);
        if (const Group* g = groupOfSrc_(x.id)) x.group = g->id;
        // TotalMix' colour wins; the positional palette fills the rest.
        if (x.colour.empty()) x.colour = kPalette[i % 8];
        ++i;
        return x;
    };
    const Performer* p = perfOfMix_(mix);
    if (p && !p->useLayout) {
        auto once = [&](const std::string& id) {
            return std::none_of(out.begin(), out.end(), [&](const Src& s) { return s.id == id; });
        };
        auto visible = [&](const std::string& kind, int n) {
            const auto& vis = kind == "in" ? visIn_ : visPb_;
            return std::find(vis.begin(), vis.end(), n) != vis.end();
        };
        // ⛔ A GROUP COMES WHOLE. The list says what shows, the submix what is heard:
        // a member left out would be in the headphones with no fader to reach it.
        // Only a group on the list counts (Frank 04.11.2026: take it off the list and
        // it is off the phone); its channels listed one by one are plain faders then.
        auto listed = [&](const Group& g) {
            return std::any_of(p->faders.begin(), p->faders.end(),
                               [&](const Fader& f) { return f.src == "group:" + g.id; });
        };
        auto wholeGroup = [&](const Group& g) {
            for (const std::string& m : g.members) {
                std::string kind; int n = 0;
                if (splitSrc(m, kind, n) && visible(kind, n) && once(m)) out.push_back(look(kind, n));
            }
        };
        for (const Fader& f : p->faders) {
            if (f.src.rfind("group:", 0) == 0) {
                for (const Group& g : groups_)
                    if (f.src == "group:" + g.id) wholeGroup(g);
                continue;
            }
            std::string kind; int n = 0;
            if (!splitSrc(f.src, kind, n) || !visible(kind, n)) continue;
            // A member of a listed group waits in the file: the group's own row says
            // where it goes, this one comes back when the group leaves the list.
            if (const Group* g = groupOfSrc_(f.src); g && listed(*g)) continue;
            if (!once(f.src)) continue;
            Src x = look(kind, n);
            x.group.clear();
            out.push_back(std::move(x));
        }
        for (const Fader& f : p->faders)
            for (Src& x : out) {
                if (x.id != f.src) continue;
                if (!f.name.empty())   x.label  = f.name;
                if (!f.colour.empty()) x.colour = f.colour;
            }
        return out;
    }
    for (const int n : visIn_) out.push_back(look("in", n));
    for (const int n : visPb_) out.push_back(look("pb", n));
    return out;
}

bool Mixer::srcAllowed_(const std::string& mix, const std::string& src) const
{
    if (!perfOfMix_(mix)) return true;
    for (const Src& x : sourcesOf_(mix))
        if (x.id == src) return true;
    return false;
}

std::string Mixer::sourcesJson_(const std::string& mix) const
{
    std::string o = "[";
    bool first = true;
    const bool perf = perfOfMix_(mix) != nullptr;
    for (const Src& x : sourcesOf_(mix)) {
        if (!first) o += ",";
        first = false;
        o += "{\"id\":" + jstr(x.id) + ",\"kind\":" + jstr(x.kind)
           + ",\"n\":" + std::to_string(x.n) + ",\"label\":" + jstr(x.label)
           + ",\"max\":" + std::to_string(maxOf(x.kind)) + ",\"color\":" + jstr(x.colour);
        if (!x.group.empty()) o += ",\"group\":" + jstr(x.group);
        if (perf) o += ",\"eq\":" + std::to_string(stripLevel_(mix, x.id, "eq"))
                     + ",\"dyn\":" + std::to_string(stripLevel_(mix, x.id, "dyn"))
                     + ",\"fx\":" + std::to_string(stripLevel_(mix, x.id, "fx"));
        o += "}";
    }
    return o + "]";
}

// The group's level is its loudest member's. Everyone else is that many dB below,
// and stays so in the memo while the group has them under the floor.
double Mixer::groupDb_(int bus, const Group& g, const std::vector<Src>& members)
{
    double top = -300.0;
    for (const Src& x : members) top = std::max(top, shownDb_(bus, x.kind, x.n));
    GroupMemo& memo = groupMemo_[std::to_string(bus) + ":" + g.id];
    if (top <= -300.0) return -300.0;
    for (const Src& x : members) {
        const double db = shownDb_(bus, x.kind, x.n);
        if (db > -300.0) memo[x.id] = db - top;
    }
    return top;
}

std::string Mixer::groupFieldsJson_(int bus, const Group& g, const std::vector<Src>& members)
{
    bool allMuted = !members.empty(), allSolo = !members.empty();
    for (const Src& x : members) {
        const auto mm = mute_.find(memoKey(bus, x.id));
        if (mm == mute_.end() || !mm->second.muted) allMuted = false;
        if (!st_.mixSoloed(busOfKind(x.kind), x.n, bus)) allSolo = false;
    }
    return "\"group\":" + jstr(g.id) + ",\"db\":" + jnum(groupDb_(bus, g, members))
         + ",\"mute\":" + (allMuted ? "1" : "0") + ",\"solo\":" + (allSolo ? "1" : "0");
}

// The groups this mix has, in the order of their first member.
std::string Mixer::groupsJson_(const std::string& mix)
{
    const int bus = busOfMix_(mix);
    std::string o = "[";
    bool first = true;
    std::vector<std::string> seen;
    for (const Src& x : sourcesOf_(mix)) {
        if (x.group.empty() || std::find(seen.begin(), seen.end(), x.group) != seen.end()) continue;
        seen.push_back(x.group);
        const auto g = std::find_if(groups_.begin(), groups_.end(), [&](const Group& z) { return z.id == x.group; });
        if (g == groups_.end()) continue;
        if (!first) o += ",";
        first = false;
        o += "{" + groupFieldsJson_(bus, *g, groupMembers_(mix, g->id))
           + ",\"label\":" + jstr(g->name.empty() ? std::string("Group") : g->name)
           + ",\"color\":" + jstr(g->colour) + "}";
    }
    return o + "]";
}

// Each message that can change a group ends here; what did not change stays quiet.
void Mixer::pushGroups_()
{
    if (groups_.empty()) return;
    for (auto& [id, c] : clients_) {
        if (!c.authed || c.mix.empty()) continue;
        const int bus = busOfMix_(c.mix);
        if (bus < 0) continue;
        for (const Group& g : groups_) {
            const std::vector<Src> members = groupMembers_(c.mix, g.id);
            if (members.empty()) continue;
            const std::string json = "{\"t\":\"group\",\"mix\":" + jstr(c.mix) + ","
                                   + groupFieldsJson_(bus, g, members) + "}";
            std::string& sent = c.groupSent[g.id];
            if (sent == json) continue;
            sent = json;
            send_(id, json);
        }
    }
}

// One member to a level. A muted one stays muted: the level waits for its unmute.
void Mixer::setMemberDb_(int bus, const Src& x, double db)
{
    const double v = asSent(db);
    MuteMemo& mm = mute_[memoKey(bus, x.id)];
    if (mm.muted) { mm.db = v; }
    else osc_("/mix/" + x.kind + "/" + std::to_string(x.n) + "/" + std::to_string(bus) + "/fader", static_cast<float>(v));
    const std::string src = x.id;
    broadcastBus_(bus, [&](const std::string& mix) {
        return "{\"t\":\"update\",\"mix\":" + jstr(mix) + ",\"src\":" + jstr(src) + ",\"db\":" + jnum(v) + "}";
    });
}

// The group fader: everyone by the same dB. The member with the least room up
// stops the group; a member the group took to the floor comes back with it. One
// that was pulled down on its own has no distance and stays down.
void Mixer::groupMove_(const std::string& mix, int bus, const Group& g, double db)
{
    const std::vector<Src> members = groupMembers_(mix, g.id);
    if (members.empty()) return;
    groupDb_(bus, g, members);   // the distances as they are now
    GroupMemo& memo = groupMemo_[std::to_string(bus) + ":" + g.id];
    // Nothing up and nothing remembered: all of them from the same place.
    if (std::none_of(members.begin(), members.end(), [&](const Src& x) { return memo.count(x.id) > 0; }))
        for (const Src& x : members) memo[x.id] = 0.0;
    double to = db;
    for (const Src& x : members) {
        const auto d = memo.find(x.id);
        if (d != memo.end()) to = std::min(to, maxOf(x.kind) - d->second);
    }
    constexpr double kFloor = -64.5;   // TotalMix' last level, below it the fader is off
    for (const Src& x : members) {
        const auto d = memo.find(x.id);
        if (d == memo.end()) continue;
        const double v = to + d->second;
        setMemberDb_(bus, x, (to <= -300.0 || v < kFloor) ? -300.0 : v);
    }
}

void Mixer::setMute_(int bus, const std::string& src, bool muted)
{
    std::string kind; int n = 0;
    if (!splitSrc(src, kind, n)) return;
    const std::string node = "/mix/" + kind + "/" + std::to_string(n) + "/" + std::to_string(bus);
    MuteMemo& mm = mute_[memoKey(bus, src)];
    if (mm.muted == muted) return;
    if (muted) {
        const double* v = st_.mixFader(busOfKind(kind), n, bus);
        mm.db    = v ? *v : -300.0;
        mm.muted = true;
        osc_(node + "/fader", -300.0f);
    } else {
        mm.muted = false;
        osc_(node + "/fader", static_cast<float>(mm.db));
    }
    broadcastBus_(bus, [&](const std::string& mix) {
        return "{\"t\":\"update\",\"mix\":" + jstr(mix) + ",\"src\":" + jstr(src)
             + ",\"mute\":" + (muted ? "1" : "0") + "}";
    });
}

void Mixer::setSolo_(int bus, const std::string& src, bool solo)
{
    std::string kind; int n = 0;
    if (!splitSrc(src, kind, n)) return;
    osc_("/mix/" + kind + "/" + std::to_string(n) + "/" + std::to_string(bus) + "/solo", solo ? 1.0f : 0.0f);
    broadcastBus_(bus, [&](const std::string& mix) {
        return "{\"t\":\"update\",\"mix\":" + jstr(mix) + ",\"src\":" + jstr(src)
             + ",\"solo\":" + (solo ? "1" : "0") + "}";
    });
}

std::string Mixer::snapshotJson_(const std::string& mix)
{
    const int bus = busOfMix_(mix);
    const bool perf = perfOfMix_(mix) != nullptr;
    std::string values = "{";
    bool first = true;
    for (const Src& x : sourcesOf_(mix)) {
        const rme::Bus b = busOfKind(x.kind);
        const auto pan = st_.mixPan.find(rme::State::mixKey(b, x.n, bus));
        const auto mm  = mute_.find(memoKey(bus, x.id));
        if (!first) values += ",";
        first = false;
        values += jstr(x.id) + ":{\"db\":" + jnum(shownDb_(bus, x.kind, x.n))
                + ",\"pan\":" + jnum(pan == st_.mixPan.end() ? 0.0 : pan->second)
                + ",\"mute\":" + ((mm != mute_.end() && mm->second.muted) ? "1" : "0")
                + ",\"solo\":" + (st_.mixSoloed(b, x.n, bus) ? "1" : "0");
        if (perf && stripLevel_(mix, x.id, "fx") > 0) {
            double fx = -65.0;
            if (const strip::Param* pr = strip::find("fxsend"))
                strip::value(st_, x.kind == "in" ? rmeu::Row::Input : rmeu::Row::Playback, x.n, *pr, fx);
            values += ",\"fx\":" + jnum(fx);
        }
        values += "}";
    }
    values += "}";
    double db = 0.0;
    bool   mute = false;
    if (const auto o = st_.outputs.find(bus); o != st_.outputs.end()) {
        db   = o->second.volume;
        mute = o->second.mute;
    }
    std::string masterExtra;
    if (const Performer* p = perfOfMix_(mix)) {
        masterExtra = ",\"shown\":" + std::string(p->master ? "1" : "0")
                    + ",\"eq\":" + std::to_string(stripLevel_(mix, "m", "eq"))
                    + ",\"dyn\":" + std::to_string(stripLevel_(mix, "m", "dyn"))
                    + ",\"al\":" + std::to_string(stripLevel_(mix, "m", "al"));
        masterExtra += "},\"hide\":" + std::string(p->hide ? "1" : "0");
    } else {
        masterExtra = "}";
    }
    return "{\"t\":\"snapshot\",\"mix\":" + jstr(mix)
         + ",\"sources\":" + sourcesJson_(mix) + ",\"groups\":" + groupsJson_(mix) + ",\"values\":" + values
         + ",\"master\":{\"db\":" + jnum(db) + ",\"mute\":" + (mute ? "1" : "0") + masterExtra + "}";
}

// Per client and only when it differs from what that client was last sent:
// the locked flags differ per recipient, and a /sendall round must not churn.
void Mixer::pushConfig_()
{
    for (auto& [id, c] : clients_) {
        if (!c.authed) continue;
        std::string json = configJson_(id);
        if (json == c.cfgJson) continue;
        c.cfgJson = json;
        send_(id, json);
    }
}

void Mixer::snapTo_(int id, Client& c)
{
    c.lvSent.clear();   // a new snapshot rebuilds the cards
    c.fxSent.clear();
    c.srcSig = sourcesJson_(c.mix);
    c.groupSent.clear();
    send_(id, snapshotJson_(c.mix));
}

void Mixer::resnapViewers_()
{
    for (auto& [id, c] : clients_) {
        if (!c.authed || c.mix.empty()) continue;
        if (busOfMix_(c.mix) >= 0) snapTo_(id, c);
    }
}

// ── meters ────────────────────────────────────────────────────────────────────

namespace {

// Half-dB steps; everything at or below -60 dB is one value, "off".
constexpr int kMeterFloor = -120;
int meterStep(const std::map<int, double>& lv, int ch)
{
    const auto it = lv.find(ch);
    if (it == lv.end() || !std::isfinite(it->second)) return kMeterFloor;
    return std::max(kMeterFloor, static_cast<int>(std::lround(it->second * 2.0)));
}

// [L] or [L,R] in dB, from half-dB steps.
std::string meterJson(const std::vector<int>& v)
{
    std::string o = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) o += ",";
        o += jnum(v[i] / 2.0);
    }
    return o + "]";
}

} // namespace

void Mixer::meters()
{
    // A channel's level index is its strip index, the right half of a stereo
    // channel one more (measured 21.09.2026, uf1-sidecar-rme-feasibility 13.7).
    auto steps = [](const std::map<int, rme::Channel>& strips, const std::map<int, double>& lv,
                    int n) {
        std::vector<int> v{ meterStep(lv, n) };
        const auto c = strips.find(n);
        if (c != strips.end() && c->second.stereo) v.push_back(meterStep(lv, n + 1));
        return v;
    };
    for (auto& [id, c] : clients_) {
        if (!c.authed || !c.meters) continue;
        const int bus = busOfMix_(c.mix);
        if (bus < 0) continue;
        std::string body;
        auto put = [&](const std::string& key, const std::vector<int>& v) {
            int packed = 0;   // one number per meter for the "changed?" test
            for (const int s : v) packed = packed * 1024 + (s - kMeterFloor);
            const auto old = c.lvSent.find(key);
            if (old != c.lvSent.end() && old->second == packed) return;
            c.lvSent[key] = packed;
            if (!body.empty()) body += ",";
            body += jstr(key) + ":" + meterJson(v);
        };
        put("m", steps(st_.outputs, st_.levelOut, bus));
        for (const Src& x : sourcesOf_(c.mix))
            put(x.id, x.kind == "in" ? steps(st_.inputs, st_.levelIn, x.n)
                                     : steps(st_.playbacks, st_.levelPb, x.n));
        if (!body.empty())
            send_(id, "{\"t\":\"levels\",\"mix\":" + jstr(c.mix) + ",\"lv\":{" + body + "}}");
    }
}

void Mixer::broadcastBus_(int bus, const std::function<std::string(const std::string&)>& make)
{
    for (const auto& [id, c] : clients_) {
        if (!c.authed) continue;
        if (!c.mix.empty() && busOfMix_(c.mix) == bus) send_(id, make(c.mix));
    }
}

// ── the page's messages ──────────────────────────────────────────────────────

// A connection stays outside until it has shown the PIN: no config, no
// snapshot, no broadcast, and everything but `hello` is ignored.
void Mixer::tryPin_(Client& c, const std::string& given, long long nowMs, int id)
{
    const auto rec = [&]() -> const FailRecord* {
        const auto it = fails_.find(c.key);
        return it == fails_.end() ? nullptr : &it->second;
    };
    long long left = lockLeft(rec(), nowMs);
    if (left == 0 && !given.empty()) {
        if (pin_.ok(given)) {
            fails_.erase(c.key);
            c.authed = true;
            c.cfgJson = configJson_(id);
            send_(id, c.cfgJson);
            return;
        }
        fails_[c.key] = afterFail(rec(), nowMs);
        if (fails_.size() > 1000) {   // many addresses guessing: drop the ones that no longer count
            for (auto it = fails_.begin(); it != fails_.end();) {
                if (!lockLeft(&it->second, nowMs) && nowMs - it->second.since > kWindowMs)
                    it = fails_.erase(it);
                else
                    ++it;
            }
        }
        left = lockLeft(rec(), nowMs);
        if (log_)
            log_("MoreMe: wrong PIN from " + c.key
                 + (left ? ", locked for " + std::to_string((left + 30000) / 60000) + " min" : ""));
    }
    send_(id, std::string("{\"t\":\"pin\",\"wrong\":") + ((!given.empty() && !left) ? "1" : "0")
                  + ",\"wait\":" + std::to_string((left + 999) / 1000) + "}");
}

void Mixer::message(int id, const std::string& json, long long nowMs)
{
    const auto it = clients_.find(id);
    if (it == clients_.end()) return;
    Client& c = it->second;
    Msg m;
    if (!parseMsg(json, m)) return;
    if (m.t == "hello" && validToken(m.performer)) c.token = m.performer;
    if (!c.authed) {
        if (m.t == "hello") tryPin_(c, m.pinIsString ? m.pin : std::string(), nowMs, id);
        return;
    }

    if (m.t == "hello") {
        c.cfgJson = configJson_(id);
        send_(id, c.cfgJson);
        return;
    }
    if (m.t == "select") {
        if (m.mix.rfind("perf:", 0) == 0) {
            // ⇨ NO LOCK ON A PERFORMER: the link is theirs, and a second device of
            // theirs moves the same mix. A link opens that performer only.
            if (!c.token.empty() && m.mix != "perf:" + c.token) return;
            if (busOfMix_(m.mix) < 0) return;
        } else {
            const int phone = phoneOf(m.mix);
            if (!phone || busOfMix_(m.mix) < 0) return;
            const int h = holderOf_(m.mix);
            if (h && h != id) {
                // Taken: the Tölpel-Sperre. Only a deliberate takeover gets past.
                if (!m.force) { send_(id, "{\"t\":\"denied\",\"mix\":" + jstr(m.mix) + "}"); return; }
                clients_[h].mix.clear();
                send_(h, "{\"t\":\"released\",\"mix\":" + jstr(m.mix) + "}");
            }
        }
        c.mix = m.mix;
        c.stripTarget.clear();
        snapTo_(id, c);
        refresh_ = true;   // re-read the layout, so a newly shown channel turns up
        pushConfig_();     // the claim changed: everybody's locked flags
        return;
    }

    if (m.t == "meters") {
        c.meters = m.on;
        c.lvSent.clear();   // back on: every meter once
        return;
    }

    // ⛔ A performer's mix answers to whoever is on it, and to what the sound
    // engineer gave them: the page hides the rest, this is what enforces it.
    const Performer* perf = perfOfMix_(m.mix);
    if (perf && c.mix != m.mix) return;
    const int bus = busOfMix_(m.mix);
    if (bus < 0) return;
    if (!m.src.empty() && !srcAllowed_(m.mix, m.src)) return;

    if (m.t == "stripOpen") {
        c.stripTarget = stripLevel_(m.mix, m.target, "any") > 0 ? m.target : std::string();
        c.stripSent.clear();
        int row = 0, ch = 0;
        if (!c.stripTarget.empty() && parseTarget_(m.mix, c.stripTarget, row, ch)) {
            // TotalMix' rounds may not carry a channel's EQ: ask for it.
            const std::string ask = strip::sendChanAddress(static_cast<rmeu::Row>(row), ch);
            if (!ask.empty()) oscQ_.push_back({ ask, 1.0f });
        }
        pushStrips_();
        return;
    }
    if (m.t == "stripClose") {
        c.stripTarget.clear();
        c.stripSent.clear();
        return;
    }
    if (m.t == "strip") {
        if (!stripMayWrite_(m.mix, m.target, m.param)) return;
        int row = 0, ch = 0;
        const strip::Param* pr = strip::find(m.param);
        if (!pr || !parseTarget_(m.mix, m.target, row, ch)) return;
        for (const auto& [addr, v] : strip::setTo(st_, static_cast<rmeu::Row>(row), ch, *pr, m.val))
            osc_(addr, v);
        pushStrips_();
        if (m.param == "fxsend") pushFx_();
        return;
    }
    if (m.t == "master" && perf && !perf->master) return;

    if (m.t == "groupSet" || m.t == "groupMute" || m.t == "groupSolo") {
        const auto g = std::find_if(groups_.begin(), groups_.end(), [&](const Group& z) { return z.id == m.group; });
        if (g == groups_.end()) return;
        const std::vector<Src> members = groupMembers_(m.mix, g->id);
        if (members.empty()) return;
        if (m.t == "groupSet") {
            groupMove_(m.mix, bus, *g, clampDb(m.val, 6.0));
        } else if (m.t == "groupMute") {
            // All muted: all back. Otherwise the rest join in.
            const bool all = std::all_of(members.begin(), members.end(), [&](const Src& x) {
                const auto mm = mute_.find(memoKey(bus, x.id));
                return mm != mute_.end() && mm->second.muted;
            });
            for (const Src& x : members) setMute_(bus, x.id, !all);
        } else {
            const bool all = std::all_of(members.begin(), members.end(), [&](const Src& x) {
                return st_.mixSoloed(busOfKind(x.kind), x.n, bus);
            });
            for (const Src& x : members) setSolo_(bus, x.id, !all);
        }
        pushGroups_();
        return;
    }

    if (m.t == "set") {
        std::string kind; int n = 0;
        if (!splitSrc(m.src, kind, n)) return;
        const std::string node = "/mix/" + kind + "/" + std::to_string(n) + "/" + std::to_string(bus);
        const std::string src = m.src;
        if (m.ctrl == "fader") {
            const double db = asSent(clampDb(m.val, maxOf(kind)));
            MuteMemo& mm = mute_[memoKey(bus, src)];
            const bool wasMuted = mm.muted;   // dragging = an explicit level: unmute
            mm.muted = false;
            osc_(node + "/fader", static_cast<float>(db));
            // Down on its own: it no longer comes back with the group.
            if (db <= -300.0)
                if (const Group* g = groupOfSrc_(src))
                    groupMemo_[std::to_string(bus) + ":" + g->id].erase(src);
            broadcastBus_(bus, [&](const std::string& mix) {
                return "{\"t\":\"update\",\"mix\":" + jstr(mix) + ",\"src\":" + jstr(src)
                     + ",\"db\":" + jnum(db) + (wasMuted ? ",\"mute\":0}" : "}");
            });
            pushGroups_();
        } else if (m.ctrl == "pan") {
            const double pan = asSent(clampPan(m.val));
            osc_(node + "/balpan", static_cast<float>(pan));
            broadcastBus_(bus, [&](const std::string& mix) {
                return "{\"t\":\"update\",\"mix\":" + jstr(mix) + ",\"src\":" + jstr(src)
                     + ",\"pan\":" + jnum(pan) + "}";
            });
        }
        return;
    }
    if (m.t == "mute") {
        // No mute per routing in Global OSC: ride the fader to -inf and back,
        // and keep the level the phones show, so the fader only dims.
        const auto mm = mute_.find(memoKey(bus, m.src));
        setMute_(bus, m.src, !(mm != mute_.end() && mm->second.muted));
        pushGroups_();
        return;
    }
    if (m.t == "solo") {   // solo per routing IS native: /mix/<kind>/<n>/<bus>/solo
        std::string kind; int n = 0;
        if (!splitSrc(m.src, kind, n)) return;
        setSolo_(bus, m.src, !st_.mixSoloed(busOfKind(kind), n, bus));
        pushGroups_();
        return;
    }
    if (m.t == "master") {
        const std::string out = "/output/" + std::to_string(bus);
        if (m.ctrl == "vol") {
            const double db = asSent(clampDb(m.val, 0.0));
            osc_(out + "/volume", static_cast<float>(db));
            broadcastBus_(bus, [&](const std::string& mix) {
                return "{\"t\":\"master\",\"mix\":" + jstr(mix) + ",\"db\":" + jnum(db) + "}";
            });
        } else if (m.ctrl == "mute") {
            const auto o = st_.outputs.find(bus);
            const bool mute = !(o != st_.outputs.end() && o->second.mute);
            osc_(out + "/mute", mute ? 1.0f : 0.0f);
            broadcastBus_(bus, [&](const std::string& mix) {
                return "{\"t\":\"master\",\"mix\":" + jstr(mix) + ",\"mute\":" + (mute ? "1" : "0") + "}";
            });
        }
        return;
    }
}

// ── TotalMix to the phones ───────────────────────────────────────────────────

void Mixer::sync(const rme::State& st, const rme::Manager::Visible& vis, bool tmSeen)
{
    tmSeen_ = tmSeen;

    // Values: what changed since the phones were last told, by bridge.js'
    // ingest rules. Unchanged values (a round repeats them all) stay quiet.
    auto decode = [](std::uint64_t key, rme::Bus& b, int& ch, int& sub) {
        b   = static_cast<rme::Bus>(key >> 40);
        ch  = static_cast<int>((key >> 20) & 0xFFFFF);
        sub = static_cast<int>(key & 0xFFFFF);
    };
    auto srcOf = [](rme::Bus b, int ch) {
        return std::string(b == rme::Bus::Input ? "in:" : "pb:") + std::to_string(ch);
    };
    for (const auto& [key, v] : st.mix) {
        const auto old = st_.mix.find(key);
        if (old != st_.mix.end() && old->second == v) continue;
        rme::Bus b; int ch = 0, bus = 0;
        decode(key, b, ch, bus);
        const std::string src = srcOf(b, ch);
        // Down in TotalMix: as down on its own on the phone (groupMove_).
        if (v <= -300.0)
            if (const Group* g = groupOfSrc_(src)) {
                const auto muted = mute_.find(memoKey(bus, src));
                if (muted == mute_.end() || !muted->second.muted)
                    groupMemo_[std::to_string(bus) + ":" + g->id].erase(src);
            }
        const auto mm = mute_.find(memoKey(bus, src));
        if (mm != mute_.end() && mm->second.muted) {
            if (v <= -300.0) continue;   // our own mute: keep the level shown
            mm->second.muted = false;    // moved in TotalMix: unmuted
            broadcastBus_(bus, [&](const std::string& mix) {
                return "{\"t\":\"update\",\"mix\":" + jstr(mix) + ",\"src\":" + jstr(src)
                     + ",\"db\":" + jnum(v) + ",\"mute\":0}";
            });
            continue;
        }
        broadcastBus_(bus, [&](const std::string& mix) {
            return "{\"t\":\"update\",\"mix\":" + jstr(mix) + ",\"src\":" + jstr(src)
                 + ",\"db\":" + jnum(v) + "}";
        });
    }
    for (const auto& [key, v] : st.mixPan) {
        const auto old = st_.mixPan.find(key);
        if (old != st_.mixPan.end() && old->second == v) continue;
        rme::Bus b; int ch = 0, bus = 0;
        decode(key, b, ch, bus);
        const std::string src = srcOf(b, ch);
        broadcastBus_(bus, [&](const std::string& mix) {
            return "{\"t\":\"update\",\"mix\":" + jstr(mix) + ",\"src\":" + jstr(src)
                 + ",\"pan\":" + jnum(v) + "}";
        });
    }
    for (const auto& [key, v] : st.mixSolo) {
        // Missing reads as off.
        const auto old = st_.mixSolo.find(key);
        if ((old != st_.mixSolo.end() && old->second) == v) continue;
        rme::Bus b; int ch = 0, bus = 0;
        decode(key, b, ch, bus);
        const std::string src = srcOf(b, ch);
        broadcastBus_(bus, [&](const std::string& mix) {
            return "{\"t\":\"update\",\"mix\":" + jstr(mix) + ",\"src\":" + jstr(src)
                 + ",\"solo\":" + (v ? "1" : "0") + "}";
        });
    }
    for (const auto& [bus, o] : st.outputs) {
        const auto old = st_.outputs.find(bus);
        const double oldDb   = old != st_.outputs.end() ? old->second.volume : 0.0;
        const bool   oldMute = old != st_.outputs.end() && old->second.mute;
        if (o.volume != oldDb)
            broadcastBus_(bus, [&](const std::string& mix) {
                return "{\"t\":\"master\",\"mix\":" + jstr(mix) + ",\"db\":" + jnum(o.volume) + "}";
            });
        if (o.mute != oldMute)
            broadcastBus_(bus, [&](const std::string& mix) {
                return "{\"t\":\"master\",\"mix\":" + jstr(mix) + ",\"mute\":" + (o.mute ? "1" : "0") + "}";
            });
    }

    // ⛔ THE PHONES' BUS BEFORE THE NEW STATE TAKES OVER: the broadcasts above
    // went to whoever sat on that bus a moment ago. Now the new state.
    st_ = st;
    if (vis.round != round_) {
        round_ = vis.round;
        visIn_ = vis.in;
        visPb_ = vis.pb;
    }

    // The source list (visible set, names, colours) lives in the snapshot:
    // when it changes, whoever it changed for gets a new one.
    for (auto& [id, c] : clients_) {
        if (!c.authed || c.mix.empty()) continue;
        // No output right now (a remote that reconnects starts empty): the first
        // state with one is new to this client.
        if (busOfMix_(c.mix) < 0) { c.srcSig.clear(); continue; }
        if (sourcesJson_(c.mix) != c.srcSig) snapTo_(id, c);
    }
    pushConfig_();
    pushStrips_();
    pushFx_();
    pushGroups_();
}

// ── EQ, dynamics, FX send ────────────────────────────────────────────────────

namespace {

// By group, what a level unlocks. Ids from RmeStrip's catalogue.
const char* const kEqSimple[]  = { "eq_on", "b1gain", "b2gain", "b3gain" };
const char* const kEqFull[]    = { "b1freq", "b1q", "b1type", "b2freq", "b2q", "b3freq", "b3q", "b3type",
                                   "lc_on", "lc_freq", "lc_slope" };
const char* const kDynSimple[] = { "dyn_on", "compthres", "compratio", "dyngain" };
const char* const kDynFull[]   = { "attack", "release", "expthres", "expratio" };
const char* const kAuto[]      = { "al_on", "maxgain", "headroom", "risetime" };

template <std::size_t N>
bool in(const char* const (&list)[N], const std::string& id)
{
    return std::any_of(list, list + N, [&](const char* x) { return id == x; });
}

bool hasParam(const rme::State& st, rmeu::Row r, int ch, const char* id)
{
    const strip::Param* p = strip::find(id);
    return p && strip::available(st, r, ch, *p);
}

} // namespace

// "m" is the mix's own output, else a source of the mix.
bool Mixer::parseTarget_(const std::string& mix, const std::string& target, int& row, int& ch) const
{
    if (target == "m") {
        ch  = busOfMix_(mix);
        row = static_cast<int>(rmeu::Row::Output);
        return ch >= 0;
    }
    std::string kind;
    if (!splitSrc(target, kind, ch)) return false;
    row = static_cast<int>(kind == "in" ? rmeu::Row::Input : rmeu::Row::Playback);
    return true;
}

// What this mix may do to a target, 0 off, 1 simple, 2 advanced, and only what
// TotalMix has on that channel. Group "any": the highest of them.
int Mixer::stripLevel_(const std::string& mix, const std::string& target, const std::string& group) const
{
    const Performer* p = perfOfMix_(mix);
    int rowI = 0, ch = 0;
    if (!p || !parseTarget_(mix, target, rowI, ch)) return 0;
    if (target != "m" && !srcAllowed_(mix, target)) return 0;
    const auto row = static_cast<rmeu::Row>(rowI);
    const bool own = target == "m";
    auto level = [&](const std::string& g) -> int {
        if (g == "eq")
            return hasParam(st_, row, ch, "eq_on") ? (own ? (p->masterEq ? 2 : 0) : p->chanEq) : 0;
        if (g == "dyn")
            return hasParam(st_, row, ch, "dyn_on") ? (own ? (p->masterDyn ? 2 : 0) : p->chanDyn) : 0;
        if (g == "al")   // on a channel Auto Level is the advanced end of dynamics
            return hasParam(st_, row, ch, "al_on") ? (own ? (p->masterAuto ? 2 : 0) : (p->chanDyn == 2 ? 2 : 0)) : 0;
        if (g == "fx")
            return !own && p->fxSend && hasParam(st_, row, ch, "fxsend") ? 1 : 0;
        return 0;
    };
    if (group == "any") return std::max({ level("eq"), level("dyn"), level("al"), level("fx") });
    return level(group);
}

bool Mixer::stripMayWrite_(const std::string& mix, const std::string& target, const std::string& param) const
{
    if (param == "fxsend") return stripLevel_(mix, target, "fx") > 0;
    if (in(kEqSimple, param))  return stripLevel_(mix, target, "eq") >= 1;
    if (in(kEqFull, param))    return stripLevel_(mix, target, "eq") >= 2;
    if (in(kDynSimple, param)) return stripLevel_(mix, target, "dyn") >= 1;
    if (in(kDynFull, param))   return stripLevel_(mix, target, "dyn") >= 2;
    if (in(kAuto, param))      return stripLevel_(mix, target, "al") >= 2;
    return false;
}

// The page for one target: levels, values, ranges, and the curve from the graph's
// own maths on this side, so the phone holds no second copy of it.
std::string Mixer::stripJson_(const std::string& mix, const std::string& target) const
{
    int rowI = 0, ch = 0;
    if (!parseTarget_(mix, target, rowI, ch)) return {};
    const auto row = static_cast<rmeu::Row>(rowI);
    std::string vals = "{";
    bool first = true;
    auto add = [&](const char* id) {
        const strip::Param* p = strip::find(id);
        double v = 0.0;
        if (!p || !strip::available(st_, row, ch, *p) || !strip::value(st_, row, ch, *p, v)) return;
        if (!first) vals += ",";
        first = false;
        vals += jstr(id) + ":{\"v\":" + jnum(v) + ",\"lo\":" + jnum(p->lo) + ",\"hi\":" + jnum(p->hi)
              + ",\"step\":" + jnum(p->step);
        // ⛔ A list's names come from the catalogue: band 3's order is not band 1's
        // (memory rme-eq-band3-indices), and a second copy on the phone would get
        // that wrong.
        if (p->kind == strip::Kind::List) {
            vals += ",\"names\":[";
            for (std::size_t i = 0; i < p->names.size(); ++i) vals += (i ? "," : "") + jstr(p->names[i]);
            vals += "]";
        }
        vals += "}";
    };
    const int eq = stripLevel_(mix, target, "eq"), dyn = stripLevel_(mix, target, "dyn"),
              al = stripLevel_(mix, target, "al");
    if (eq >= 1)  for (const char* id : kEqSimple)  add(id);
    if (eq >= 2)  for (const char* id : kEqFull)    add(id);
    if (dyn >= 1) for (const char* id : kDynSimple) add(id);
    if (dyn >= 2) for (const char* id : kDynFull)   add(id);
    if (al >= 2)  for (const char* id : kAuto)      add(id);
    vals += "}";
    std::string curve = "[";
    if (eq >= 1) {
        const uf1eq::Model model = rmeu::eqModel(st_, row, ch);
        constexpr int kPoints = 121;
        for (int i = 0; i < kPoints; ++i) {
            const double f = 20.0 * std::pow(1000.0, static_cast<double>(i) / (kPoints - 1));
            double db = 0.0;
            if (model.on)
                for (const uf1eq::Band& b : model.bands) db += uf1eq::bandDb(b, f);
            char t[16];
            snprintf(t, sizeof(t), "%.2f", std::clamp(db, -30.0, 30.0));
            if (i) curve += ",";
            curve += t;
        }
    }
    curve += "]";
    return "{\"t\":\"strip\",\"mix\":" + jstr(mix) + ",\"target\":" + jstr(target)
         + ",\"eq\":" + std::to_string(eq) + ",\"dyn\":" + std::to_string(dyn)
         + ",\"al\":" + std::to_string(al) + ",\"vals\":" + vals + ",\"curve\":" + curve + "}";
}

void Mixer::pushStrips_()
{
    for (auto& [id, c] : clients_) {
        if (!c.authed || c.stripTarget.empty() || c.mix.empty()) continue;
        if (stripLevel_(c.mix, c.stripTarget, "any") <= 0) {   // taken away, or gone
            c.stripTarget.clear();
            c.stripSent.clear();
            send_(id, "{\"t\":\"stripClosed\"}");
            continue;
        }
        std::string json = stripJson_(c.mix, c.stripTarget);
        if (json == c.stripSent) continue;
        c.stripSent = json;
        send_(id, std::move(json));
    }
}

void Mixer::pushFx_()
{
    const strip::Param* pr = strip::find("fxsend");
    if (!pr) return;
    for (auto& [id, c] : clients_) {
        if (!c.authed || c.mix.empty()) continue;
        const Performer* p = perfOfMix_(c.mix);
        if (!p || !p->fxSend) continue;
        for (const Src& x : sourcesOf_(c.mix)) {
            if (stripLevel_(c.mix, x.id, "fx") <= 0) continue;
            double v = -65.0;
            if (!strip::value(st_, x.kind == "in" ? rmeu::Row::Input : rmeu::Row::Playback, x.n, *pr, v)) continue;
            const auto old = c.fxSent.find(x.id);
            if (old != c.fxSent.end() && old->second == v) continue;
            const bool firstTime = old == c.fxSent.end();
            c.fxSent[x.id] = v;
            if (!firstTime)   // the snapshot had it
                send_(id, "{\"t\":\"update\",\"mix\":" + jstr(c.mix) + ",\"src\":" + jstr(x.id)
                              + ",\"fx\":" + jnum(v) + "}");
        }
    }
}

} // namespace orc::phones
