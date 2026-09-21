#include "RmeState.h"

#include <cstdlib>

namespace reasixty::rme {
namespace {

// "/input/38/eq/band1freq" -> section "input", index 38, leaf "eq/band1freq".
// Returns false when the address is not of that shape at all.
bool splitChannel(const std::string& addr, std::string& section,
                  int& index, std::string& leaf)
{
    if (addr.size() < 2 || addr[0] != '/') return false;
    const std::size_t s1 = addr.find('/', 1);
    if (s1 == std::string::npos) return false;
    section = addr.substr(1, s1 - 1);
    const std::size_t s2 = addr.find('/', s1 + 1);
    if (s2 == std::string::npos) return false;
    const std::string num = addr.substr(s1 + 1, s2 - s1 - 1);
    if (num.empty()) return false;
    for (const char c : num) if (c < '0' || c > '9') return false;
    index = std::atoi(num.c_str());
    leaf  = addr.substr(s2 + 1);
    return !leaf.empty();
}

std::map<int, Channel>* mapFor(State& st, const std::string& section)
{
    if (section == "input")    return &st.inputs;
    if (section == "playback") return &st.playbacks;
    if (section == "output")   return &st.outputs;
    return nullptr;
}

bool truthy(const Message& m)
{
    return !m.args.empty() && m.args[0].number() >= 0.5;
}

double num(const Message& m)
{
    return m.args.empty() ? 0.0 : m.args[0].number();
}

}  // namespace

namespace {
// faderlin 0.00, 0.05 .. 1.00 -> dB, measured 2026-09-21 (see RmeState.h).
constexpr double kCurve[21] = {
    kDbOff,   -57.5783, -50.6309, -44.1578, -38.1589, -32.6343, -27.584,
    -23.0079, -18.9061, -15.2786, -12.1254, -9.44641, -7.24171, -5.48824,
    -3.84706, -2.20588, -0.564707, 1.07647, 2.71764, 4.35882, 6.0 };
}  // namespace

double faderlinToDb(double x)
{
    if (!(x > 0.0)) return kDbOff;           // also catches NaN
    if (x >= 1.0) return kCurve[20];
    const double pos = x * 20.0;
    const int    i   = static_cast<int>(pos);
    const double t   = pos - i;
    // Between OFF and the first real point there is no dB to interpolate
    // against, so the first step is drawn from the next two points' slope.
    if (i == 0) {
        const double slope = kCurve[2] - kCurve[1];
        return kCurve[1] - (1.0 - t) * slope * 4.0;   // steep towards silence
    }
    return kCurve[i] + t * (kCurve[i + 1] - kCurve[i]);
}

double dbToFaderlin(double db)
{
    if (!(db > kDbOff + 1.0)) return 0.0;
    if (db >= kCurve[20]) return 1.0;
    if (db < kCurve[1]) {                     // below 0.05: invert the first step
        const double slope = kCurve[2] - kCurve[1];
        const double t = 1.0 - (kCurve[1] - db) / (slope * 4.0);
        return t > 0.0 ? t * 0.05 : 0.0;
    }
    for (int i = 1; i < 20; ++i) {
        if (db <= kCurve[i + 1]) {
            const double t = (db - kCurve[i]) / (kCurve[i + 1] - kCurve[i]);
            return (i + t) * 0.05;
        }
    }
    return 1.0;
}

const Channel* State::outputForRole(int channelIndex) const
{
    if (channelIndex < 0) return nullptr;
    const auto it = outputs.find(channelIndex);
    if (it == outputs.end() || !it->second.seen) return nullptr;
    return &it->second;
}

bool ingest(State& st, const Message& m)
{
    ++st.ingested;
    const std::string& a = m.address;

    // ── control room ────────────────────────────────────────────────────────
    if (a.rfind("/controlroom/", 0) == 0) {
        const std::string leaf = a.substr(13);
        if (leaf == "mainout")      { st.mainOut      = static_cast<int>(num(m)); return true; }
        if (leaf == "mainoutb")     { st.mainOutB     = static_cast<int>(num(m)); return true; }
        if (leaf == "phones1")      { st.phones[0]    = static_cast<int>(num(m)); return true; }
        if (leaf == "phones2")      { st.phones[1]    = static_cast<int>(num(m)); return true; }
        if (leaf == "phones3")      { st.phones[2]    = static_cast<int>(num(m)); return true; }
        if (leaf == "phones4")      { st.phones[3]    = static_cast<int>(num(m)); return true; }
        if (leaf == "talkchannel")  { st.talkChannel  = static_cast<int>(num(m)); return true; }
        if (leaf == "cuechan")      { st.cueChannel   = static_cast<int>(num(m)); return true; }
        if (leaf == "extinchannel") { st.extInChannel = static_cast<int>(num(m)); return true; }
        if (leaf == "dim")          { st.dim          = truthy(m); return true; }
        if (leaf == "mainmono")     { st.mono         = truthy(m); return true; }
        if (leaf == "speakerb")     { st.speakerB     = truthy(m); return true; }
        if (leaf == "talkback")     { st.talkback     = truthy(m); return true; }
        if (leaf == "externalin")   { st.externalIn   = truthy(m); return true; }
        if (leaf == "mutefx")       { st.muteFx       = truthy(m); return true; }
        if (leaf == "linkab")       { st.linkAB       = truthy(m); return true; }
        if (leaf == "dimreduction") { st.dimReduction = num(m); return true; }
        if (leaf == "recallvolume") { st.recallVolume = num(m); return true; }
        if (leaf == "extingain")    { st.extInGain    = num(m); return true; }
        return false;
    }

    if (a == "/globalmute") { st.globalMute = truthy(m); return true; }
    if (a == "/globalsolo") { st.globalSolo = truthy(m); return true; }

    // ── snapshots: 0 off, 2 active, 3 changed ───────────────────────────────
    if (a.rfind("/snapshot/load/", 0) == 0) {
        const int n = std::atoi(a.c_str() + 15);          // slots count from 1
        if (n >= 1 && n <= 8) {
            const int v = static_cast<int>(num(m));
            st.snapshot[n - 1] = (v == 2) ? SnapshotState::Active
                               : (v == 3) ? SnapshotState::Changed
                                          : SnapshotState::Off;
            return true;
        }
        return false;
    }

    // ── levels ──────────────────────────────────────────────────────────────
    // Shape is /level/<bus>/<channel>, so splitChannel does not fit: its middle
    // element is a NUMBER and this one is a word.
    if (a.rfind("/level/", 0) == 0) {
        const std::size_t s2 = a.find('/', 7);
        if (s2 == std::string::npos) return false;
        const std::string bus = a.substr(7, s2 - 7);
        const int ch = std::atoi(a.c_str() + s2 + 1);
        if (bus == "in")  { st.levelIn[ch]  = num(m); return true; }
        if (bus == "pb")  { st.levelPb[ch]  = num(m); return true; }
        if (bus == "out") { st.levelOut[ch] = num(m); return true; }
        return false;
    }

    // ── submix nodes: /mix/<in|pb>/<channel>/<submix>/fader ─────────────────
    if (a.rfind("/mix/", 0) == 0) {
        const std::size_t s1 = a.find('/', 5);
        if (s1 == std::string::npos) return false;
        const std::string bus = a.substr(5, s1 - 5);
        const Bus b = (bus == "in") ? Bus::Input : (bus == "pb") ? Bus::Playback : Bus::Output;
        if (b == Bus::Output) return false;
        const std::size_t s2 = a.find('/', s1 + 1);
        if (s2 == std::string::npos) return false;
        const std::size_t s3 = a.find('/', s2 + 1);
        if (s3 == std::string::npos) return false;
        const int ch  = std::atoi(a.c_str() + s1 + 1);
        const int sub = std::atoi(a.c_str() + s2 + 1);
        if (a.compare(s3 + 1, std::string::npos, "fader") == 0) {
            st.mix[State::mixKey(b, ch, sub)] = num(m);
            return true;
        }
        return false;   // balpan, solo, groupflags: known, not used yet
    }

    // ── channel strips ──────────────────────────────────────────────────────
    std::string section, leaf; int idx = 0;
    if (!splitChannel(a, section, idx, leaf)) return false;
    std::map<int, Channel>* mp = mapFor(st, section);
    if (!mp) return false;
    Channel& ch = (*mp)[idx];
    // ⛔ "IT SENT SOMETHING" IS NOT "IT IS A STRIP". Several parameters are
    // addressed on the RIGHT half of a stereo pair by index + 1 (the L/R column
    // of the table): phase, delay, gain, every room-EQ band. So a first pass
    // that marked a channel seen on ANY message counted 94 outputs on Frank's
    // rig where the mixer shows 5 — the odd indices were the right halves,
    // nameless and with colour -1, and the role resolver would have called one
    // of them a visible channel. A strip announces itself with a NAME: names
    // came back for exactly 61 inputs, 47 playbacks and 5 outputs, which is the
    // mixer. So `seen` follows the name and nothing else (measured 2026-09-19).
    if (leaf == "name")   { if (!m.args.empty() && m.args[0].type == Arg::Type::String) {
                                ch.name = m.args[0].s;
                                ch.seen = true;
                            }
                            return true; }
    if (leaf == "color")  { ch.colour = static_cast<int>(num(m)); return true; }
    if (leaf == "volume") { ch.volume = num(m); return true; }
    if (leaf == "mute")   { ch.mute   = truthy(m); return true; }
    if (leaf == "stereo") { ch.stereo = truthy(m); return true; }
    // The channel EQ. Band leaves are "eq/band<N><what>", N = 1..3.
    if (leaf.rfind("eq/", 0) == 0) {
        ChannelEq& e = ch.eq;
        e.seen = true;
        const std::string w = leaf.substr(3);
        if (w == "enable")    { e.on    = truthy(m); return true; }
        if (w == "band1type") { e.type1 = static_cast<int>(num(m)); return true; }
        if (w == "band3type") { e.type3 = static_cast<int>(num(m)); return true; }
        if (w.size() > 5 && w.rfind("band", 0) == 0 && w[4] >= '1' && w[4] <= '3') {
            const int k = w[4] - '1';
            const std::string what = w.substr(5);
            if (what == "freq") { e.freq[k] = num(m); return true; }
            if (what == "gain") { e.gain[k] = num(m); return true; }
            if (what == "q")    { e.q[k]    = num(m); return true; }
        }
        return false;
    }
    if (leaf.rfind("lowcut/", 0) == 0) {
        ChannelEq& e = ch.eq;
        e.seen = true;
        const std::string w = leaf.substr(7);
        if (w == "enable") { e.lcOn    = truthy(m); return true; }
        if (w == "freq")   { e.lcFreq  = num(m); return true; }
        if (w == "slope")  { e.lcSlope = static_cast<int>(num(m)); return true; }
        return false;
    }
    // Everything else on a strip is known-but-unused today. The channel is
    // still marked seen, which is what the role check reads.
    return false;
}

}  // namespace reasixty::rme
