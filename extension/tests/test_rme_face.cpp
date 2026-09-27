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

    // ── the key lamps (Frank 27.09.: "LEDs mit Funktion leuchten lassen") ────
    {
        namespace b = ::uf1::btn;
        using L = face::Lamp;
        auto lampOf = [](const std::array<face::KeyLamp, 18>& ls, std::uint8_t btn) {
            for (const auto& l : ls) if (l.btn == btn) return l.lamp;
            return L::Lit;   // missing = a failure below, never a pass
        };
        face::LampFacts f;
        f.online = true; f.haveMain = true; f.haveChannel = true;
        f.secondHalf = true; f.moreRight = true;
        auto ls = face::keyLamps(f);
        EXPECT(lampOf(ls, b::kArrowLeft) == L::Dark);    // first bank: nothing left
        EXPECT(lampOf(ls, b::kArrowRight) == L::Lit);
        // Half 1 on the keys: lit toward the half there is, dark where there is
        // none (Frank 27.09.: the first version had it "genau falsch herum").
        EXPECT(lampOf(ls, b::kBankLeft) == L::Dark);
        EXPECT(lampOf(ls, b::kBankRight) == L::Lit);
        EXPECT(lampOf(ls, b::k5to8) == L::Dark);
        EXPECT(lampOf(ls, b::kNavUp) == L::Dim && lampOf(ls, b::kNavLeft) == L::Dim
               && lampOf(ls, b::kNavRight) == L::Dim && lampOf(ls, b::kNavDown) == L::Dim);
        EXPECT(lampOf(ls, b::kNavCentre) == L::Dim);     // TotalMix hidden
        EXPECT(lampOf(ls, b::kMaster) == L::Dim);
        EXPECT(lampOf(ls, b::kChannelSoftKey) == L::Dim); // mono
        EXPECT(lampOf(ls, b::kFlip) == L::Dark && lampOf(ls, b::kScrub) == L::Dark
               && lampOf(ls, b::kT1) == L::Dark && lampOf(ls, b::kT2) == L::Dark
               && lampOf(ls, b::kTLeft) == L::Dark && lampOf(ls, b::kTRight) == L::Dark);

        f.half = 1; f.window = true; f.mainOnFader = true; f.stereo = true;
        ls = face::keyLamps(f);
        EXPECT(lampOf(ls, b::kBankLeft) == L::Lit && lampOf(ls, b::kBankRight) == L::Dark);
        EXPECT(lampOf(ls, b::kNavCentre) == L::Lit);
        EXPECT(lampOf(ls, b::kMaster) == L::Lit);
        EXPECT(lampOf(ls, b::kChannelSoftKey) == L::Lit);

        // No second half, or STRIP: Bank ◄ ► do nothing, so they are dark.
        f.secondHalf = false;
        ls = face::keyLamps(f);
        EXPECT(lampOf(ls, b::kBankLeft) == L::Dark && lampOf(ls, b::kBankRight) == L::Dark);
        f.secondHalf = true; f.strip = true;
        ls = face::keyLamps(f);
        EXPECT(lampOf(ls, b::kBankLeft) == L::Dark && lampOf(ls, b::kBankRight) == L::Dark);
        // No link, no Main, no channel: those keys do nothing.
        f = face::LampFacts{};
        ls = face::keyLamps(f);
        EXPECT(lampOf(ls, b::kNavCentre) == L::Dark && lampOf(ls, b::kMaster) == L::Dark
               && lampOf(ls, b::kChannelSoftKey) == L::Dark);

        // ⛔ NO PASS-THROUGH KEY IN THE LIST: those lamps are the host's, and a
        // second writer is the one thing this split exists to prevent.
        for (const auto& l : ls)
            EXPECT(!reasixty::rme::input::passesThrough(l.btn));

        // The bytes: dim = white quartered, FF39 0x11; lit = white, 0x00.
        std::vector<std::vector<std::uint8_t>> out;
        const face::Out o{ [&](std::vector<std::uint8_t> fr) { out.push_back(std::move(fr)); },
                           [&](std::vector<std::uint8_t> fr) { out.push_back(std::move(fr)); } };
        const face::KeyLamp two[2] = { { b::kMaster, L::Dim }, { b::kArrowRight, L::Lit } };
        int cache[2] = { 0, 0 };
        auto navAt = std::chrono::steady_clock::now();
        face::emitLamps(two, 2, cache, navAt, o, /*force*/ false);
        const std::uint8_t mLed = b::kMaster - 0x18, aLed = b::kArrowRight - 0x18;
        EXPECT(out.size() == 4);
        EXPECT(out[0] == ::uf1::buildColourRgb(mLed, 0x3F3F3Fu));
        EXPECT(out[1] == ::uf1::buildLedLevel(mLed, 0x11));
        EXPECT(out[2] == ::uf1::buildColourRgb(aLed, 0xFFFFFFu));
        EXPECT(out[3] == ::uf1::buildLedLevel(aLed, 0x00));
        out.clear();
        face::emitLamps(two, 2, cache, navAt, o, false);
        EXPECT(out.empty());                              // unchanged: nothing sent
    }

    if (g_fail == 0) std::printf("test_rme_face: all good\n");
    return g_fail ? 1 : 0;
}
