// Uf8Strips.h: one UF8 strip as a controller surface (docs/orc-uf8-plan.md
// stage 5). Pins the dedup (cold: everything; unchanged: nothing; one cell
// changed: that cell), the order of the cells, the motor rules, the rings.

#include "Uf8Strips.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

using namespace uf8strips;
using Frame = std::vector<std::uint8_t>;

struct Store {
    std::string upper, cs = "\x01", chan, ft, fu, vl, sl;
    std::int32_t top = -1;
    std::int8_t sel = -1, cut = -1, solo = -1;
    std::uint16_t pb = 0xFFFF;
    Cells cells() { return Cells{upper, cs, chan, ft, fu, vl, sl, top, sel, cut, solo, pb}; }
};

struct Out {
    std::vector<Frame> frames;
    int leds = 0;
    auto send() { return [this](Frame f) { frames.push_back(std::move(f)); }; }
    auto led()  { return [this](uf8::LedColourFrames) { ++leds; }; }
};

static View sample()
{
    View v;
    v.upper = "Phones 1"; v.csType = "OUT"; v.chanNum = "9";
    v.faderText = "-12.5"; v.faderUnit = "dB";
    v.valueLine = "Pan            C";
    v.slotLabel = "DIM";
    v.topKey = 1; v.sel = true; v.cut = false; v.solo = false;
    v.drive = true; v.faderPb = 9000;
    return v;
}

int main()
{
    // Cold: every cell, in the field-map order, then the motor.
    {
        Store c; Out o; bool engaged = false;
        const View v = sample();
        const bool drove = paintStrip(c.cells(), v, 2, false, engaged, o.send(), o.led());
        EXPECT(drove && engaged);
        EXPECT(o.leds == 4);   // soft key, SEL, CUT, SOLO
        // upper, cs, chan, readout, value line, slot label, then 3 motor frames
        EXPECT(o.frames.size() == 9);
        if (o.frames.size() == 9) {
            EXPECT(o.frames[0] == uf8::buildStripTextUpper(2, "Phones 1"));
            EXPECT(o.frames[1] == uf8::buildChannelStripType(2, "OUT"));
            EXPECT(o.frames[2] == uf8::buildChannelNumber(2, "9"));
            EXPECT(o.frames[3] == uf8::buildFaderDbReadout(2, "-12.5", "dB"));
            EXPECT(o.frames[4] == uf8::buildValueLine(2, "Pan            C"));
            EXPECT(o.frames[5] == uf8::buildPluginSlotName(2, "DIM"));
            EXPECT(o.frames[6] == uf8fader::echo(2, 9000));
            EXPECT(o.frames[7] == uf8::buildMotorEnable(2, true));
        }
        // Unchanged: nothing at all.
        Out o2;
        EXPECT(!paintStrip(c.cells(), v, 2, false, engaged, o2.send(), o2.led()));
        EXPECT(o2.frames.empty() && o2.leds == 0);
        // One cell changed: that cell.
        View w = v; w.valueLine = "Pan          L20";
        Out o3;
        paintStrip(c.cells(), w, 2, false, engaged, o3.send(), o3.led());
        EXPECT(o3.frames.size() == 1 && o3.leds == 0);
        EXPECT(!o3.frames.empty() && o3.frames[0] == uf8::buildValueLine(2, "Pan          L20"));
        // The fader moved, motor engaged: the value alone.
        w.faderPb = 9100;
        Out o4;
        EXPECT(paintStrip(c.cells(), w, 2, false, engaged, o4.send(), o4.led()));
        EXPECT(o4.frames.size() == 1);
        // Touched: the motor stays off the hand.
        w.faderPb = 9200;
        Out o5;
        EXPECT(!paintStrip(c.cells(), w, 2, true, engaged, o5.send(), o5.led()));
        EXPECT(o5.frames.empty());
        // Released after a touch (the caller set engaged = false on touch):
        // echo, enable, value again.
        engaged = false;
        Out o6;
        EXPECT(paintStrip(c.cells(), w, 2, false, engaged, o6.send(), o6.led()));
        EXPECT(o6.frames.size() == 3);
        // Nothing to drive to: the motor is left alone.
        View n = w; n.drive = false; n.faderPb = 100;
        Out o7;
        EXPECT(!paintStrip(c.cells(), n, 2, false, engaged, o7.send(), o7.led()));
    }
    // A lamp changes: one LED pair.
    {
        Store c; Out o; bool engaged = true;
        View v = sample(); v.drive = false;
        paintStrip(c.cells(), v, 0, false, engaged, o.send(), o.led());
        View w = v; w.cut = true;
        Out o2;
        paintStrip(c.cells(), w, 0, false, engaged, o2.send(), o2.led());
        EXPECT(o2.leds == 1 && o2.frames.empty());
    }
    // Rings: positions before modes, each only when it changed.
    {
        std::uint16_t lastBar[8] = {};
        std::uint8_t  lastMode[8] = {};
        std::array<std::uint16_t, 8> bar{}; bar[3] = 50;
        std::array<std::uint8_t, 8> mode{}; mode.fill(0x03); mode[3] = 0x01;
        std::vector<Frame> f;
        auto send = [&](Frame x) { f.push_back(std::move(x)); };
        paintRings(lastBar, lastMode, false, bar, mode, send);
        EXPECT(f.size() == 2);
        if (f.size() == 2) {
            EXPECT(f[0] == uf8::buildVPotReadoutBar(bar));
            EXPECT(f[1] == uf8::buildVPotMode(mode));
        }
        f.clear();
        paintRings(lastBar, lastMode, true, bar, mode, send);
        EXPECT(f.empty());
        bar[3] = 60;
        paintRings(lastBar, lastMode, true, bar, mode, send);
        EXPECT(f.size() == 1 && f[0] == uf8::buildVPotReadoutBar(bar));
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("uf8_strips: all passed\n");
    return 0;
}
