#include "Phones.h"

#include "JsonTree.h"
#include "RmeNames.h"
#include "Sha256.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

namespace orc::phones {

namespace rme = reasixty::rme;

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
            std::snprintf(b, sizeof(b), "\\u%04x", c);
            o += b;
        } else o.push_back(ch);
    }
    return o + "\"";
}

std::string jnum(double v)
{
    if (!std::isfinite(v)) return "null";   // what JSON.stringify writes
    char b[32];
    std::snprintf(b, sizeof(b), "%.9g", v);
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
    std::string t, pin, mix, src, ctrl;
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
    std::string mixes = "[";
    bool first = true;
    for (int p = 1; p <= 4; ++p) {
        const int bus = busOfPhone_(p);
        if (bus < 0) continue;
        const std::string id = "phone" + std::to_string(p);
        std::string label = "Phones " + std::to_string(p);
        std::string colour;
        if (const auto o = st_.outputs.find(bus); o != st_.outputs.end()) {
            if (!o->second.name.empty()) label = o->second.name;
            colour = colourHex(o->second.colour);
        }
        if (!names_[p - 1].empty()) label = names_[p - 1];
        if (colour.empty()) colour = kPalette[(p - 1) % 8];
        const int h = holderOf_(id);
        if (!first) mixes += ",";
        first = false;
        mixes += "{\"id\":" + jstr(id) + ",\"phone\":" + std::to_string(p)
               + ",\"bus\":" + std::to_string(bus) + ",\"label\":" + jstr(label)
               + ",\"color\":" + jstr(colour)
               + ",\"locked\":" + ((h != 0 && h != forId) ? "true" : "false") + "}";
    }
    mixes += "]";
    return "{\"t\":\"config\",\"version\":" + jstr(version_) + ",\"tmSeen\":"
         + (tmSeen_ ? "true" : "false") + ",\"mixes\":" + mixes + "}";
}

// The shared visible set, inputs then playbacks, as the page lists it.
std::string Mixer::sourcesJson_() const
{
    std::string o = "[";
    int i = 0;
    auto add = [&](const std::string& kind, int n) {
        const auto& m = kind == "in" ? st_.inputs : st_.playbacks;
        std::string label, colour;
        if (const auto c = m.find(n); c != m.end()) {
            label  = c->second.name;
            colour = colourHex(c->second.colour);
        }
        if (label.empty()) label = (kind == "in" ? "IN " : "PB ") + std::to_string(n);
        // TotalMix' colour wins; the positional palette fills the rest.
        if (colour.empty()) colour = kPalette[i % 8];
        if (i++) o += ",";
        o += "{\"id\":" + jstr(kind + ":" + std::to_string(n)) + ",\"kind\":" + jstr(kind)
           + ",\"n\":" + std::to_string(n) + ",\"label\":" + jstr(label)
           + ",\"max\":" + std::to_string(maxOf(kind)) + ",\"color\":" + jstr(colour) + "}";
    };
    for (const int n : visIn_) add("in", n);
    for (const int n : visPb_) add("pb", n);
    return o + "]";
}

std::string Mixer::snapshotJson_(int phone) const
{
    const int bus = busOfPhone_(phone);
    std::string values = "{";
    bool first = true;
    auto add = [&](const std::string& kind, int n) {
        const rme::Bus b = busOfKind(kind);
        const std::string id = kind + ":" + std::to_string(n);
        const auto pan = st_.mixPan.find(rme::State::mixKey(b, n, bus));
        const auto mm  = mute_.find(memoKey(bus, id));
        if (!first) values += ",";
        first = false;
        values += jstr(id) + ":{\"db\":" + jnum(shownDb_(bus, kind, n))
                + ",\"pan\":" + jnum(pan == st_.mixPan.end() ? 0.0 : pan->second)
                + ",\"mute\":" + ((mm != mute_.end() && mm->second.muted) ? "1" : "0")
                + ",\"solo\":" + (st_.mixSoloed(b, n, bus) ? "1" : "0") + "}";
    };
    for (const int n : visIn_) add("in", n);
    for (const int n : visPb_) add("pb", n);
    values += "}";
    double db = 0.0;
    bool   mute = false;
    if (const auto o = st_.outputs.find(bus); o != st_.outputs.end()) {
        db   = o->second.volume;
        mute = o->second.mute;
    }
    return "{\"t\":\"snapshot\",\"mix\":" + jstr("phone" + std::to_string(phone))
         + ",\"sources\":" + sourcesJson_() + ",\"values\":" + values
         + ",\"master\":{\"db\":" + jnum(db) + ",\"mute\":" + (mute ? "1" : "0") + "}}";
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

void Mixer::resnapViewers_()
{
    for (auto& [id, c] : clients_) {
        if (!c.authed) continue;
        const int p = phoneOf(c.mix);
        if (p && busOfPhone_(p) >= 0) {
            c.lvSent.clear();   // a new snapshot rebuilds the cards
            send_(id, snapshotJson_(p));
        }
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
        const int p = phoneOf(c.mix);
        const int bus = busOfPhone_(p);
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
        for (const int n : visIn_) put("in:" + std::to_string(n), steps(st_.inputs, st_.levelIn, n));
        for (const int n : visPb_) put("pb:" + std::to_string(n), steps(st_.playbacks, st_.levelPb, n));
        if (!body.empty())
            send_(id, "{\"t\":\"levels\",\"mix\":" + jstr(c.mix) + ",\"lv\":{" + body + "}}");
    }
}

void Mixer::broadcastBus_(int bus, const std::function<std::string(const std::string&)>& make)
{
    for (const auto& [id, c] : clients_) {
        if (!c.authed) continue;
        const int p = phoneOf(c.mix);
        if (p && busOfPhone_(p) == bus) send_(id, make("phone" + std::to_string(p)));
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
        const int phone = phoneOf(m.mix);
        if (!phone || busOfPhone_(phone) < 0) return;
        const int h = holderOf_(m.mix);
        if (h && h != id) {
            // Taken: the Tölpel-Sperre. Only a deliberate takeover gets past.
            if (!m.force) { send_(id, "{\"t\":\"denied\",\"mix\":" + jstr(m.mix) + "}"); return; }
            clients_[h].mix.clear();
            send_(h, "{\"t\":\"released\",\"mix\":" + jstr(m.mix) + "}");
        }
        c.mix = m.mix;
        c.lvSent.clear();   // a new headphone: every meter once
        send_(id, snapshotJson_(phone));
        refresh_ = true;   // re-read the layout, so a newly shown channel turns up
        pushConfig_();     // the claim changed: everybody's locked flags
        return;
    }

    if (m.t == "meters") {
        c.meters = m.on;
        c.lvSent.clear();   // back on: every meter once
        return;
    }

    const int phone = phoneOf(m.mix);
    const int bus   = busOfPhone_(phone);
    if (bus < 0) return;

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
            broadcastBus_(bus, [&](const std::string& mix) {
                return "{\"t\":\"update\",\"mix\":" + jstr(mix) + ",\"src\":" + jstr(src)
                     + ",\"db\":" + jnum(db) + (wasMuted ? ",\"mute\":0}" : "}");
            });
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
        std::string kind; int n = 0;
        if (!splitSrc(m.src, kind, n)) return;
        const std::string node = "/mix/" + kind + "/" + std::to_string(n) + "/" + std::to_string(bus);
        const std::string src = m.src;
        MuteMemo& mm = mute_[memoKey(bus, src)];
        if (!mm.muted) {
            const double* v = st_.mixFader(busOfKind(kind), n, bus);
            mm.db    = v ? *v : -300.0;
            mm.muted = true;
            osc_(node + "/fader", -300.0f);
        } else {
            mm.muted = false;
            osc_(node + "/fader", static_cast<float>(mm.db));
        }
        const bool muted = mm.muted;
        broadcastBus_(bus, [&](const std::string& mix) {
            return "{\"t\":\"update\",\"mix\":" + jstr(mix) + ",\"src\":" + jstr(src)
                 + ",\"mute\":" + (muted ? "1" : "0") + "}";
        });
        return;
    }
    if (m.t == "solo") {   // solo per routing IS native: /mix/<kind>/<n>/<bus>/solo
        std::string kind; int n = 0;
        if (!splitSrc(m.src, kind, n)) return;
        const bool solo = !st_.mixSoloed(busOfKind(kind), n, bus);
        osc_("/mix/" + kind + "/" + std::to_string(n) + "/" + std::to_string(bus) + "/solo",
             solo ? 1.0f : 0.0f);
        const std::string src = m.src;
        broadcastBus_(bus, [&](const std::string& mix) {
            return "{\"t\":\"update\",\"mix\":" + jstr(mix) + ",\"src\":" + jstr(src)
                 + ",\"solo\":" + (solo ? "1" : "0") + "}";
        });
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
    // when it changes, everybody on a headphone gets a new one.
    std::string sources = sourcesJson_();
    if (sources != lastSources_) {
        lastSources_ = std::move(sources);
        resnapViewers_();
    }
    pushConfig_();
}

} // namespace orc::phones
