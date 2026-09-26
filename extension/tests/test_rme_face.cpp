// What TotalMix looks like on the UF1: src/RmeFace.cpp, the side-car painter.
//
// ⇨ WHY THIS TEST. On 2026-09-25 ORC ran its own copy of this painter, built
// from memory, and Frank's first look at the glass found no pot labels, no
// colour bars and letters in the seven-segment field. The painter is shared
// now; this pins the three things that were wrong, on the shared one, so a
// change that breaks them fails here and not on the desk.

#include "RmeFace.h"
#include "UF1Protocol.h"
#include "Uf1Text.h"

#include <cstdio>
#include <deque>
#include <string>
#include <vector>

using namespace reasixty::rme;
namespace face = reasixty::rme::face;
namespace in_  = reasixty::rme::input;

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

namespace {

struct Rec {
    std::vector<std::vector<std::uint8_t>> frames;
    uf1spread::VpotRow row;
    bool rowSeen = false;
    bool wrote(std::uint16_t addr) const { return find(addr) != nullptr; }
    const std::vector<std::uint8_t>* find(std::uint16_t addr) const
    {
        for (const auto& f : frames)
            for (std::size_t i = 0; i + 1 < f.size(); ++i)
                if (static_cast<std::uint16_t>((f[i] << 8) | f[i + 1]) == addr) return &f;
        return nullptr;
    }
};

// The two frames paint() would put on the wire for a plain rows-and-values
// mixer: Main at output 0, Phones 1 at output 6.
void seed()
{
    Config cfg;
    cfg.enabled = true;
    manager().setConfig(cfg);
}

} // namespace

int main()
{
    seed();
    // The painter reads the manager's snapshot. With nothing ingested the link
    // counts as down, so the first pass is the "no TotalMix" face; that alone
    // already pins the labels and the layer.
    Rec rec;
    face::Host h;
    h.emitVpotRow = [&](const uf1spread::VpotRow& r, bool) { rec.row = r; rec.rowSeen = true; };
    face::Out o{ [&](std::vector<std::uint8_t> f) { rec.frames.push_back(std::move(f)); },
                 [&](std::vector<std::uint8_t> f) { rec.frames.push_back(std::move(f)); } };
    face::Cache c;
    in_::State in;
    face::paint(c, in, h, o, /*force*/ true);

    // ⛔ THE LAYER. Pot names are only drawn in Layout 1, so a forced pass must
    // enter it: 0x0100 = {01, 00} after the two-stage {00, 01}.
    {
        const auto want = ::uf1::buildScreen(0x0100, std::vector<std::uint8_t>{ 0x01, 0x00 });
        bool entered = false;
        for (const auto& f : rec.frames) if (f == want) entered = true;
        EXPECT(entered);
    }
    // ⛔ THE LABELS go through the host's row emitter as a Layout 1 row, and the
    // first pot says why there is nothing to show.
    EXPECT(rec.rowSeen);
    EXPECT(rec.row.layout1);
    EXPECT(rec.row.names[0] == "RME");
    // ⛔ THE COLOUR BARS over the pots, Layout 1's own element.
    EXPECT(rec.wrote(::uf1::scr::kColourBars4));
    // ⛔ THE TIME FIELD goes out as segment masks, never as plain text.
    if (const auto* tc = rec.find(::uf1::scr::kTimecode)) {
        const auto blank = ::uf1::buildScreen(::uf1::scr::kTimecode, ::uf1::seg7Payload(""));
        EXPECT(*tc == blank);   // no link, no jog value: all eleven cells dark
    } else {
        EXPECT(false && "time field written");
    }

    // An unchanged second pass writes the big screen's cached parts again only
    // where they changed: nothing here changed.
    const std::size_t before = rec.frames.size();
    face::paint(c, in, h, o, /*force*/ false);
    EXPECT(!rec.find(0x0100) || rec.frames.size() >= before);   // no re-entry needed
    std::size_t reentries = 0;
    const auto sel = ::uf1::buildScreen(0x0100, std::vector<std::uint8_t>{ 0x01, 0x00 });
    for (std::size_t i = before; i < rec.frames.size(); ++i)
        if (rec.frames[i] == sel) ++reentries;
    EXPECT(reentries == 0);

    // ⛔ THE MOTOR GETS ITS TARGET BEFORE IT IS ENGAGED (26.09.). The device's
    // queue puts a priority frame in FRONT of everything waiting; engaging first
    // made the fader hop to its last stored target. Modelled here the way
    // UF1Device queues: send at the back, sendPriority at the front.
    {
        std::deque<std::vector<std::uint8_t>> wire;
        face::Out qo{ [&](std::vector<std::uint8_t> f) { wire.push_back(std::move(f)); },
                      [&](std::vector<std::uint8_t> f) { wire.push_front(std::move(f)); } };
        face::Cache qc;
        face::paint(qc, in, h, qo, /*force*/ true);
        long posAt = -1, onAt = -1;
        for (std::size_t i = 0; i < wire.size(); ++i) {
            const auto& f = wire[i];
            if (f.size() >= 2 && f[0] == 0xFF && f[1] == 0x1E && posAt < 0) posAt = long(i);
            if (f == ::uf1::buildMotorEnable(true) && onAt < 0) onAt = long(i);
        }
        EXPECT(posAt >= 0 && onAt >= 0);
        EXPECT(posAt < onAt);
    }

    // Off reads "-", as in TotalMix (Frank 25.09.), not REAPER's "-inf".
    EXPECT(face::dbText(reasixty::rme::kDbOff) == "-");
    EXPECT(face::dbText(-64.5).rfind("-64", 0) == 0);

    if (g_fail == 0) std::printf("test_rme_face: all good\n");
    return g_fail ? 1 : 0;
}
