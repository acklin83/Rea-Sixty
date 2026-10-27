// MidiCcLayer: fader and V-Pot values, keys, the messages that go out, what comes
// back (14 bit included), the hand-wins rule and the CC lanes in items.

#include "MidiCcLayer.h"

#include <cstdio>
#include <cstdlib>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

using namespace midicc;

static Control cc(int number, int channel = 0, bool hr = false)
{
    Control c; c.number = number; c.channel = channel; c.hiRes = hr; return c;
}

int main()
{
    // ---- channel, 14 bit, names ------------------------------------------------
    EXPECT(channelOf(cc(1), 3) == 3);
    EXPECT(channelOf(cc(1, 9), 3) == 9);
    EXPECT(channelOf(cc(1, 17), 3) == 3);          // out of range falls back
    EXPECT(channelOf(cc(1), 0) == 1);              // a broken default clamps
    EXPECT(hiResActive(Kind::Fader, cc(1, 0, true)));
    EXPECT(hiResActive(Kind::Fader, cc(31, 0, true)));
    EXPECT(!hiResActive(Kind::Fader, cc(32, 0, true)));   // no pair above 31
    EXPECT(!hiResActive(Kind::VPot, cc(1, 0, true)));     // faders only
    EXPECT(!hiResActive(Kind::Fader, cc(1)));
    EXPECT(displayName(Kind::Fader, cc(74)) == "CC 74");
    EXPECT(displayName(Kind::SendPlugin, cc(3)) == "PC 3");
    { Control c = cc(1); c.name = "Dynamics12"; EXPECT(displayName(Kind::Fader, c) == "Dynamics"); }
    EXPECT(displayName(Kind::Fader, Control{}).empty());  // unassigned
    EXPECT(valueText(0, false) == "--");
    EXPECT(valueText(64, true) == "64");

    // ---- fader <-> value -------------------------------------------------------
    EXPECT(faderToValue(0, false) == 0);
    EXPECT(faderToValue(kFaderPbMax, false) == 127);
    EXPECT(faderToValue(kFaderPbMax, true) == 16383);
    EXPECT(faderToValue(kFaderPbMax + 500, false) == 127);
    EXPECT(faderToValue(-3, true) == 0);
    EXPECT(faderToValue(kFaderPbMax / 2, false) == 63 || faderToValue(kFaderPbMax / 2, false) == 64);
    EXPECT(valueToFader(127, false) == kFaderPbMax);
    EXPECT(valueToFader(0, true) == 0);
    for (int v = 0; v <= 127; ++v) EXPECT(faderToValue(valueToFader(v, false), false) == v);
    for (int v = 0; v <= 16383; v += 97) EXPECT(std::abs(faderToValue(valueToFader(v, true), true) - v) <= 1);

    // ---- V-Pot -----------------------------------------------------------------
    EXPECT(vpotStep(64, 1, 1.0, false) == 65);
    EXPECT(vpotStep(64, -3, 1.0, false) == 61);
    EXPECT(vpotStep(64, 2, 2.0, false) == 68);
    EXPECT(vpotStep(64, 1, 0.25, false) == 65);    // a detent always moves
    EXPECT(vpotStep(64, -1, 0.25, false) == 63);
    EXPECT(vpotStep(64, 2, 4.0, true) == 66);      // Fine: one per detent
    EXPECT(vpotStep(126, 5, 1.0, false) == 127);
    EXPECT(vpotStep(1, -5, 1.0, false) == 0);
    EXPECT(vpotStep(40, 0, 3.0, false) == 40);

    // ---- keys ------------------------------------------------------------------
    {
        int v = 0;
        EXPECT(keyEdge(Behaviour::Toggle, true, v) && v == 127);
        EXPECT(!keyEdge(Behaviour::Toggle, false, v) && v == 127);   // release sends nothing
        EXPECT(keyEdge(Behaviour::Toggle, true, v) && v == 0);
        EXPECT(keyEdge(Behaviour::Hold, true, v) && v == 127);
        EXPECT(keyEdge(Behaviour::Hold, false, v) && v == 0);
        v = 100;                                                     // from outside
        EXPECT(keyEdge(Behaviour::Toggle, true, v) && v == 0);
    }

    // ---- FLIP ------------------------------------------------------------------
    EXPECT(faderKind(false) == Kind::Fader && ringKind(false) == Kind::VPot);
    EXPECT(faderKind(true) == Kind::VPot && ringKind(true) == Kind::Fader);
    EXPECT(normalised(16383, true) == 1.0 && normalised(0, false) == 0.0);

    // ---- outgoing ----------------------------------------------------------------
    {
        auto m = messagesFor(Kind::Fader, cc(11), 1, 100);
        EXPECT(m.size() == 1 && m[0] == (Msg{0xB0, 11, 100}));
        m = messagesFor(Kind::Fader, cc(11, 2), 1, 100);
        EXPECT(m.size() == 1 && m[0] == (Msg{0xB1, 11, 100}));
        m = messagesFor(Kind::Fader, cc(1, 0, true), 3, 0x2345);        // 14 bit, MSB first
        EXPECT(m.size() == 2 && m[0] == (Msg{0xB2, 1, 0x46}) && m[1] == (Msg{0xB2, 33, 0x45}));
        m = messagesFor(Kind::VPot, cc(1, 0, true), 1, 200);              // V-Pot never 14 bit
        EXPECT(m.size() == 1 && m[0] == (Msg{0xB0, 1, 127}));
        m = messagesFor(Kind::SendPlugin, cc(5, 16), 1, 127);
        EXPECT(m.size() == 1 && m[0] == (Msg{0xCF, 5, 0}));
        EXPECT(messagesFor(Kind::Soft, Control{}, 1, 127).empty());       // unassigned
    }

    // ---- incoming ----------------------------------------------------------------
    {
        Layer L;
        L.defaultChannel = 1;
        L.pages[0].fader[0] = cc(1, 0, true);     // 14 bit
        L.pages[0].fader[1] = cc(11);
        L.pages[2].vpot[4]  = cc(11);             // same CC on another page
        L.pages[0].soft[0]  = cc(64, 2);          // channel 2
        L.pages[1].sendPlugin[0] = cc(0);
        L.pages[1].sendPlugin[1] = cc(1);
        Assembler a;

        auto h = match(L, Msg{0xB0, 11, 90}, a);
        EXPECT(h.size() == 2);
        EXPECT(h[0].page == 0 && h[0].kind == Kind::Fader && h[0].index == 1 && h[0].value == 90);
        EXPECT(h[1].page == 2 && h[1].kind == Kind::VPot && h[1].index == 4 && h[1].value == 90);

        EXPECT(match(L, Msg{0xB1, 11, 90}, a).empty());     // wrong channel
        h = match(L, Msg{0xB1, 64, 127}, a);
        EXPECT(h.size() == 1 && h[0].kind == Kind::Soft && h[0].value == 127);

        h = match(L, Msg{0xB0, 1, 0x46}, a);               // MSB alone: LSB 0
        EXPECT(h.size() == 1 && h[0].value == (0x46 << 7));
        h = match(L, Msg{0xB0, 33, 0x45}, a);              // LSB joins the stored MSB
        EXPECT(h.size() == 1 && h[0].value == 0x2345);
        h = match(L, Msg{0xB0, 1, 0x10}, a);               // a new MSB drops the LSB
        EXPECT(h.size() == 1 && h[0].value == (0x10 << 7));

        h = match(L, Msg{0xC0, 1, 0}, a);                  // program 1: key 2 on, key 1 off
        EXPECT(h.size() == 2);
        EXPECT(h[0].index == 0 && h[0].value == 0 && h[1].index == 1 && h[1].value == 127);
        EXPECT(match(L, Msg{0x90, 60, 100}, a).empty());   // notes are not ours
    }

    // ---- the hand wins -----------------------------------------------------------
    EXPECT(handWins(1000, 0, true));
    EXPECT(!handWins(1000, 0, false));                     // never sent
    EXPECT(handWins(1000, 900, false));                    // inside the echo window
    EXPECT(!handWins(1000, 1000 - kEchoWindowMs, false));
    {
        Layer L;
        L.pages[0].fader[0] = cc(7);
        State s;
        EXPECT(!s.isKnown(0, Kind::Fader, 0));
        auto m = s.local(L, Kind::Fader, 0, 100, 5000);
        EXPECT(m.size() == 1 && m[0] == (Msg{0xB0, 7, 100}));
        EXPECT(s.local(L, Kind::Fader, 0, 100, 5010).empty());        // no repeats
        EXPECT(!s.apply(Hit{0, Kind::Fader, 0, 20}, 5050, false));   // echo window
        EXPECT(s.v(0, Kind::Fader, 0) == 100);
        EXPECT(s.apply(Hit{0, Kind::Fader, 0, 20}, 6000, false));
        EXPECT(s.v(0, Kind::Fader, 0) == 20);
        EXPECT(!s.apply(Hit{0, Kind::Fader, 0, 30}, 9000, true));    // touched
        s.stepPage(9);  EXPECT(s.page == kPages - 1);
        s.stepPage(-9); EXPECT(s.page == 0);
        s.forget();
        EXPECT(!s.isKnown(0, Kind::Fader, 0) && s.page == 0);
    }

    // ---- CC lanes in items -------------------------------------------------------
    {
        const Points p = {{0.0, 10}, {960.0, 64}, {1920.0, 127}};
        EXPECT(valueAt(p, -1.0) == -1);
        EXPECT(valueAt(p, 0.0) == 10);
        EXPECT(valueAt(p, 959.9) == 10);                   // jumps, no curve
        EXPECT(valueAt(p, 960.0) == 64);
        EXPECT(valueAt(p, 5000.0) == 127);
        EXPECT(valueAt({}, 10.0) == -1);

        const Points msb = {{0.0, 0x10}, {960.0, 0x46}};
        const Points lsb = {{0.0, 0x7F}, {480.0, 0x01}, {960.0, 0x45}};
        EXPECT(valueAt14(msb, lsb, 100.0) == ((0x10 << 7) | 0x7F));
        EXPECT(valueAt14(msb, lsb, 500.0) == ((0x10 << 7) | 0x01));
        EXPECT(valueAt14(msb, lsb, 960.0) == 0x2345);
        EXPECT(valueAt14(msb, {{100.0, 5}}, 1000.0) == (0x46 << 7));   // LSB before the MSB
        EXPECT(valueAt14({}, lsb, 500.0) == -1);
    }

    // ---- factory layout ------------------------------------------------------------
    {
        const Layer L = factoryLayer();
        EXPECT(L.outputs.size() == 1 && L.outputs[0].empty());
        EXPECT(L.pages[0].fader[0].number == 1 && hiResActive(Kind::Fader, L.pages[0].fader[0]));
        EXPECT(L.pages[0].foot[0].number == 64 && L.pages[0].foot[0].behaviour == Behaviour::Hold);
        EXPECT(!assigned(L.pages[1].fader[0]));
        // No two controls on page 1 send the same CC on the same channel.
        int seen[128] = {};
        for (int k = 0; k < kKindCount; ++k) {
            if (static_cast<Kind>(k) == Kind::SendPlugin) continue;
            for (int i = 0; i < countOf(static_cast<Kind>(k)); ++i) {
                const Control& c = L.pages[0].at(static_cast<Kind>(k), i);
                if (!assigned(c)) continue;
                EXPECT(++seen[c.number] == 1);
                if (hiResActive(static_cast<Kind>(k), c)) EXPECT(++seen[c.number + 32] == 1);
            }
        }
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("midi_cc_layer: all passed\n");
    return 0;
}
