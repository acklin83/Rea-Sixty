#pragma once
//
// MidiCcLayer: the UF8 MIDI CC layer (plan docs/midi-cc-layer-plan.md, Baustein B).
// Pure logic; main.cpp talks to REAPER and the UF8, tests/test_midi_cc_layer.cpp is a
// table. The data model below is also what Bindings.h stores per layer (Baustein C).
//
// What MIDI gives, in the two forms this layer uses:
//
//   Control Change   Bn cc vv   n = channel 0..15, cc 0..127, vv 0..127
//   Program Change   Cn pp      pp = program 0..127, one data byte
//
// 14 bit (Frank 27.10.2026: an option per fader): CC 0..31 carry the MSB, CC 32..63
// the LSB of the same controller. A sender sends the MSB first, then the LSB; a
// receiver that gets a new MSB drops the LSB it had. Only faders offer it, and only
// on CC 0..31.
//
// Measured 27.10.2026 with the probes REASIXTY_DEBUG_MIDI_IN_PROBE / _VKB_CC_PROBE:
// StuffMIDIMessage(0, ...) reaches a track recording from "Virtual MIDI keyboard";
// CreateMIDIInput opens a hardware input on macOS whether or not REAPER has it
// enabled, and REAPER keeps receiving on it. Windows: virtual ports only.
//
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace midicc {

// The soft-key bank keys pick the page: V-POT = page 0, 1..5 = pages 1..5 (Frank
// 27.10.2026: "V-Pot Soft-Key Bank vergessen, sind damit 6"; SSL's own layer has 5).
constexpr int kPages = 6;
constexpr int kStrips = 8;
constexpr int kFeet = 2;
// The UF8 fader's top position. Mirrors kUf8FaderPbMax in main.cpp, which
// static_asserts the two are equal.
constexpr int kFaderPbMax = 15583;
// Own sends and touched controls win over what comes back for this long after the
// last own send: a synth that echoes, or a CC lane one tick behind the hand, must
// not pull the fader out from under the finger.
constexpr int kEchoWindowMs = 150;

// ---- the data model ------------------------------------------------------------

// The controls the layer owns. SOLO, CUT, BANK, Quick and the rest have no function
// on a CC layer (plan section 4); transport and friends stay on Layer 1.
enum class Kind : uint8_t { Fader, VPot, Push, Soft, SendPlugin, Foot };
constexpr int kKindCount = 6;

inline int countOf(Kind k) { return k == Kind::Foot ? kFeet : kStrips; }
inline bool isKey(Kind k)  { return k == Kind::Push || k == Kind::Soft || k == Kind::Foot; }
// Whether the UF8 can show the control's colour: a fader on its SEL key and the
// colour bar, a V-Pot there too once FLIP puts it on the fader, a soft-key and a
// SEND/PLUG-IN key on their own lamp. A V-Pot push and a foot switch have none.
inline bool hasColour(Kind k) { return k != Kind::Push && k != Kind::Foot; }

// The page as the CS-type cell shows it (4 characters) and as Settings names it
// (the key's own label).
inline const char* pageCell(int p)
{
    static const char* const k[kPages] = {"VPOT", "PG1", "PG2", "PG3", "PG4", "PG5"};
    return (p >= 0 && p < kPages) ? k[p] : "";
}
inline const char* pageKey(int p)
{
    static const char* const k[kPages] = {"V-POT", "1", "2", "3", "4", "5"};
    return (p >= 0 && p < kPages) ? k[p] : "";
}

// Toggle: each press flips between 0 and 127 (SSL's only behaviour). Hold: 127 while
// held, 0 on release (sustain and the like).
enum class Behaviour : uint8_t { Toggle, Hold };

struct Control {
    int  channel   = 0;        // 0 = the layer's default channel, else 1..16
    int  number    = -1;       // CC 0..127 (SEND/PLUG-IN: program 0..127); -1 = unassigned
    bool hiRes     = false;    // faders only, CC 0..31 only (hiResActive)
    Behaviour behaviour = Behaviour::Toggle;   // keys only
    std::string name;          // shown on the UF8, 8 characters
    int  colour    = 0;        // palette index, like the binding colours
};

struct Page {
    Control fader[kStrips], vpot[kStrips], push[kStrips], soft[kStrips],
            sendPlugin[kStrips], foot[kFeet];

    Control& at(Kind k, int i)
    {
        switch (k) {
            case Kind::Fader:      return fader[i];
            case Kind::VPot:       return vpot[i];
            case Kind::Push:       return push[i];
            case Kind::Soft:       return soft[i];
            case Kind::SendPlugin: return sendPlugin[i];
            case Kind::Foot:       break;
        }
        return foot[i];
    }
    const Control& at(Kind k, int i) const { return const_cast<Page*>(this)->at(k, i); }
};

struct Layer {
    std::vector<std::string> outputs;   // device names; "" = REAPER's virtual MIDI keyboard
    std::string inputDevice;            // "" = none
    bool followItems    = true;         // follow the CC lanes of the selected track
    int  defaultChannel = 1;            // 1..16
    // On the heap: bindings' Config is ~600 KB already and must never grow on a
    // stack (Windows load crash, twice). kPages entries, always.
    std::vector<Page> pages = std::vector<Page>(kPages);
};

// The colours a control can take (Control::colour), as RGB for the UF8's palette
// matcher (ledColourForTrackRgb) and the colour bar.
constexpr int kColourCount = 8;
constexpr uint32_t kColours[kColourCount] = {
    0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x0000FF, 0xFF00FF, 0xFFFFFF };
inline uint32_t colourRgb(const Control& c)
{
    return kColours[(c.colour >= 0 && c.colour < kColourCount) ? c.colour : 0];
}

inline bool assigned(const Control& c) { return c.number >= 0 && c.number <= 127; }

inline int channelOf(const Control& c, int defaultChannel)
{
    const int ch = (c.channel >= 1 && c.channel <= 16) ? c.channel : defaultChannel;
    return std::clamp(ch, 1, 16);
}

// 14 bit only where MIDI defines the pair: a fader on CC 0..31.
inline bool hiResActive(Kind k, const Control& c)
{
    return k == Kind::Fader && c.hiRes && c.number >= 0 && c.number <= 31;
}

inline int maxValue(bool hr) { return hr ? 16383 : 127; }

// What the UF8 shows for an unnamed control.
inline std::string displayName(Kind k, const Control& c)
{
    std::string n = c.name;
    if (n.empty() && assigned(c)) {
        char b[16];
        snprintf(b, sizeof(b), k == Kind::SendPlugin ? "PC %d" : "CC %d", c.number);
        n = b;
    }
    if (n.size() > 8) n.resize(8);
    return n;
}

inline std::string valueText(int value, bool known)
{
    if (!known) return "--";
    return std::to_string(value);
}

// Factory layout, the first page (V-POT) only; the other five start unassigned. Faders: the controllers
// orchestral libraries and synths answer to most often. V-Pots and V-Pot pushes on
// CCs MIDI leaves undefined (22..29, 110..117), soft-keys on 102..109, the feet
// sustain and soft pedal, SEND/PLUG-IN programs 0..7.
inline Layer factoryLayer()
{
    Layer L;
    L.outputs = {""};
    static const int   kFaderCc[8] = {1, 11, 2, 21, 7, 10, 74, 71};
    static const char* kFaderName[8] = {"Mod", "Express", "Breath", "Vibrato",
                                        "Volume", "Pan", "Cutoff", "Reso"};
    Page& p = L.pages[0];
    for (int i = 0; i < kStrips; ++i) {
        p.fader[i].number = kFaderCc[i];
        p.fader[i].name   = kFaderName[i];
        p.vpot[i].number  = 22 + i;
        p.push[i].number  = 110 + i;
        p.soft[i].number  = 102 + i;
        p.sendPlugin[i].number = i;
    }
    p.fader[0].hiRes = true;            // CC1 is the one libraries read at 14 bit
    p.foot[0].number = 64; p.foot[0].name = "Sustain"; p.foot[0].behaviour = Behaviour::Hold;
    p.foot[1].number = 67; p.foot[1].name = "Soft";    p.foot[1].behaviour = Behaviour::Hold;
    return L;
}

// ---- fader, V-Pot, keys ------------------------------------------------------------

inline int faderToValue(int pb, bool hr)
{
    pb = std::clamp(pb, 0, kFaderPbMax);
    return static_cast<int>(std::lround(static_cast<double>(pb) * maxValue(hr) / kFaderPbMax));
}

inline int valueToFader(int value, bool hr)
{
    value = std::clamp(value, 0, maxValue(hr));
    return static_cast<int>(std::lround(static_cast<double>(value) * kFaderPbMax / maxValue(hr)));
}

// A V-Pot turn. One detent = one step times the UF8 V-Pot speed; Fine (Shift) is
// exactly one step per detent. A detent always moves at least one step.
inline int vpotStep(int value, int detents, double speed, bool fine)
{
    if (detents == 0) return value;
    int step = detents;
    if (!fine) {
        step = static_cast<int>(std::lround(detents * speed));
        if (step == 0) step = detents > 0 ? 1 : -1;
    }
    return std::clamp(value + step, 0, 127);
}

// A key edge. Returns true when a value goes out, with it in `value`.
inline bool keyEdge(Behaviour b, bool pressed, int& value)
{
    if (b == Behaviour::Hold) {
        value = pressed ? 127 : 0;
        return true;
    }
    if (!pressed) return false;
    value = value >= 64 ? 0 : 127;
    return true;
}

// FLIP on the CC layer swaps faders and V-Pots: the fader drives the V-Pot's
// controller and the ring shows the fader's.
inline Kind faderKind(bool flip) { return flip ? Kind::VPot : Kind::Fader; }
inline Kind ringKind(bool flip)  { return flip ? Kind::Fader : Kind::VPot; }

// A value as 0..1, for the ring and for a fader showing a V-Pot controller.
inline double normalised(int value, bool hr)
{
    return std::clamp(static_cast<double>(value) / maxValue(hr), 0.0, 1.0);
}

// ---- outgoing messages ----------------------------------------------------------

struct Msg {
    uint8_t status = 0, d1 = 0, d2 = 0;
    bool operator==(const Msg& o) const { return status == o.status && d1 == o.d1 && d2 == o.d2; }
};

// The messages for one control taking `value`. 14 bit: MSB first, then LSB.
// Program Change has one data byte; d2 stays 0 and the sender drops it.
inline std::vector<Msg> messagesFor(Kind k, const Control& c, int defaultChannel, int value)
{
    std::vector<Msg> out;
    if (!assigned(c)) return out;
    const uint8_t ch = static_cast<uint8_t>(channelOf(c, defaultChannel) - 1);
    if (k == Kind::SendPlugin) {
        out.push_back({static_cast<uint8_t>(0xC0 | ch), static_cast<uint8_t>(c.number), 0});
        return out;
    }
    if (hiResActive(k, c)) {
        value = std::clamp(value, 0, 16383);
        out.push_back({static_cast<uint8_t>(0xB0 | ch), static_cast<uint8_t>(c.number),
                       static_cast<uint8_t>(value >> 7)});
        out.push_back({static_cast<uint8_t>(0xB0 | ch), static_cast<uint8_t>(c.number + 32),
                       static_cast<uint8_t>(value & 0x7F)});
        return out;
    }
    out.push_back({static_cast<uint8_t>(0xB0 | ch), static_cast<uint8_t>(c.number),
                   static_cast<uint8_t>(std::clamp(value, 0, 127))});
    return out;
}

// ---- incoming messages ------------------------------------------------------------

// Keeps the last MSB per channel and controller 0..31, so an LSB can be put together
// with it. A new MSB clears the LSB, as the MIDI spec asks.
struct Assembler {
    int msb[16][32];
    Assembler() { for (auto& row : msb) for (int& v : row) v = 0; }
};

struct Hit {
    int  page  = 0;
    Kind kind  = Kind::Fader;
    int  index = 0;
    int  value = 0;
};

// Every control on every page that an incoming message sets. A Program Change lights
// the SEND/PLUG-IN key with that program and darkens the others on its channel.
inline std::vector<Hit> match(const Layer& L, const Msg& m, Assembler& a)
{
    std::vector<Hit> hits;
    const int type = m.status & 0xF0;
    const int ch   = (m.status & 0x0F) + 1;
    if (type == 0xC0) {
        for (int p = 0; p < kPages; ++p)
            for (int i = 0; i < kStrips; ++i) {
                const Control& c = L.pages[p].sendPlugin[i];
                if (!assigned(c) || channelOf(c, L.defaultChannel) != ch) continue;
                hits.push_back({p, Kind::SendPlugin, i, c.number == m.d1 ? 127 : 0});
            }
        return hits;
    }
    if (type != 0xB0) return hits;
    const int cc = m.d1, v = m.d2 & 0x7F;
    if (cc < 32) a.msb[ch - 1][cc] = v;
    for (int p = 0; p < kPages; ++p)
        for (int k = 0; k < kKindCount; ++k) {
            const Kind kind = static_cast<Kind>(k);
            if (kind == Kind::SendPlugin) continue;
            for (int i = 0; i < countOf(kind); ++i) {
                const Control& c = L.pages[p].at(kind, i);
                if (!assigned(c) || channelOf(c, L.defaultChannel) != ch) continue;
                if (hiResActive(kind, c)) {
                    if (cc == c.number)           hits.push_back({p, kind, i, v << 7});
                    else if (cc == c.number + 32) hits.push_back({p, kind, i, (a.msb[ch - 1][c.number] << 7) | v});
                } else if (cc == c.number) {
                    hits.push_back({p, kind, i, v});
                }
            }
        }
    return hits;
}

// Whether a value from outside may move this control now.
inline bool handWins(int64_t nowMs, int64_t lastOwnSendMs, bool touched)
{
    return touched || (lastOwnSendMs > 0 && nowMs - lastOwnSendMs < kEchoWindowMs);
}

// ---- following CC lanes in MIDI items -------------------------------------------

// One controller's points in an item, sorted by position (any unit, here PPQ).
using Points = std::vector<std::pair<double, int>>;

// The value at `pos`: the last point at or before it, -1 when there is none yet.
// Between points the value jumps (plan section 6: no curve shapes).
inline int valueAt(const Points& pts, double pos)
{
    auto it = std::upper_bound(pts.begin(), pts.end(), pos,
        [](double p, const std::pair<double, int>& e) { return p < e.first; });
    if (it == pts.begin()) return -1;
    return std::prev(it)->second;
}

// The same for a 14-bit controller from its MSB and LSB lanes. An LSB counts only
// when it comes at or after the MSB it refines.
inline int valueAt14(const Points& msb, const Points& lsb, double pos)
{
    auto im = std::upper_bound(msb.begin(), msb.end(), pos,
        [](double p, const std::pair<double, int>& e) { return p < e.first; });
    if (im == msb.begin()) return -1;
    const auto& m = *std::prev(im);
    auto il = std::upper_bound(lsb.begin(), lsb.end(), pos,
        [](double p, const std::pair<double, int>& e) { return p < e.first; });
    int low = 0;
    if (il != lsb.begin() && std::prev(il)->first >= m.first) low = std::prev(il)->second;
    return ((m.second & 0x7F) << 7) | (low & 0x7F);
}

// ---- runtime state ------------------------------------------------------------

// Values live only in memory (plan section 6: forgotten on project change). A value
// is unknown until the hand or a return path sets it; until then the fader stays
// where it is and nothing is sent.
struct State {
    int     page = 0;
    int     value[kPages][kKindCount][kStrips] = {};
    bool    known[kPages][kKindCount][kStrips] = {};
    int64_t lastSendMs[kPages][kKindCount][kStrips] = {};

    void forget() { *this = State{}; }

    void setPage(int p) { page = std::clamp(p, 0, kPages - 1); }
    void stepPage(int delta) { setPage(page + delta); }

    int& v(int p, Kind k, int i) { return value[p][static_cast<int>(k)][i]; }
    bool& isKnown(int p, Kind k, int i) { return known[p][static_cast<int>(k)][i]; }
    int64_t& sentAt(int p, Kind k, int i) { return lastSendMs[p][static_cast<int>(k)][i]; }

    // A value from outside (items or hardware). False when the hand wins and the
    // value is dropped.
    bool apply(const Hit& h, int64_t nowMs, bool touched)
    {
        if (handWins(nowMs, sentAt(h.page, h.kind, h.index), touched)) return false;
        v(h.page, h.kind, h.index) = h.value;
        isKnown(h.page, h.kind, h.index) = true;
        return true;
    }

    // A value from the hand. Returns the messages to send; empty when the value did
    // not change (nothing is repeated).
    std::vector<Msg> local(const Layer& L, Kind k, int i, int newValue, int64_t nowMs)
    {
        const Control& c = L.pages[page].at(k, i);
        int& cur = v(page, k, i);
        bool& kn = isKnown(page, k, i);
        if (kn && cur == newValue && k != Kind::SendPlugin) return {};
        cur = newValue;
        kn = true;
        sentAt(page, k, i) = nowMs;
        return messagesFor(k, c, L.defaultChannel, newValue);
    }
};

}  // namespace midicc
