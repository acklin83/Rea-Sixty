// Uf8Frames.h: UF8 bulk-IN packets to whole frames (docs/orc-uf8-plan.md,
// stage 2). The cases are the ones main.cpp's comments name: split frames,
// the 31 xx prefix on a continuation (forum 2026-10-25, pb 12337), a packet
// that is only the prefix (cap51 at 36.038), the position checksum.

#include "Uf8Frames.h"

#include <cstdio>
#include <vector>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

using namespace uf8frames;
using Bytes = std::vector<std::uint8_t>;

struct Run {
    std::vector<Frame> frames;
    int unknown = 0, badck = 0;
};

static void feedInto(Assembler& a, const Bytes& b, Run& r)
{
    feed(a, b.data(), b.size(), r.frames, [&](Skip k, const std::uint8_t*, std::size_t) {
        if (k == Skip::Unknown) ++r.unknown; else ++r.badck;
    });
}

static bool same(const Frame& f, const Bytes& b)
{
    if (f.n != b.size()) return false;
    for (std::size_t k = 0; k < b.size(); ++k) if (f.b[k] != b[k]) return false;
    return true;
}

// FF 21 03 strip lsb msb sum
static Bytes pos(std::uint8_t strip, std::uint8_t lsb, std::uint8_t msb)
{
    const std::uint8_t sum = static_cast<std::uint8_t>(0x21 + 0x03 + strip + lsb + msb);
    return {0xFF, 0x21, 0x03, strip, lsb, msb, sum};
}

int main()
{
    const Bytes touch = {0xFF, 0x20, 0x02, 0x07, 0x01, 0x00};   // 6 bytes
    const Bytes poll  = {0xFF, 0x04, 0x02, 0x9d, 0x01, 0xa4};
    const Bytes btn   = {0xFF, 0x22, 0x03, 0x40, 0x00, 0x01, 0x00};
    const Bytes vpot  = {0xFF, 0x24, 0x02, 0x03, 0x41, 0x00};

    // One frame, behind a session prefix.
    {
        Assembler a; Run r;
        Bytes p = {0x31, 0x60}; p.insert(p.end(), touch.begin(), touch.end());
        feedInto(a, p, r);
        EXPECT(r.frames.size() == 1 && same(r.frames[0], touch));
        EXPECT(a.residual.empty());
    }
    // Several frames in one packet, the poll first (its 6-byte size once
    // swallowed the next frame's FF).
    {
        Assembler a; Run r;
        Bytes p = poll; for (auto v : {touch, pos(2, 0x10, 0x40), btn, vpot}) p.insert(p.end(), v.begin(), v.end());
        feedInto(a, p, r);
        EXPECT(r.frames.size() == 5);
        if (r.frames.size() == 5) {
            EXPECT(same(r.frames[0], poll));
            EXPECT(same(r.frames[1], touch));
            EXPECT(same(r.frames[2], pos(2, 0x10, 0x40)));
            EXPECT(same(r.frames[3], btn));
            EXPECT(same(r.frames[4], vpot));
        }
    }
    // A touch split across two packets: "FF 20 02" | "<strip> <state> ...".
    {
        Assembler a; Run r;
        feedInto(a, {0x31, 0x60, 0xFF, 0x20, 0x02}, r);
        EXPECT(r.frames.empty());
        EXPECT(a.residual.size() == 3);
        feedInto(a, {0x31, 0x60, 0x07, 0x01, 0x00}, r);
        EXPECT(r.frames.size() == 1 && same(r.frames[0], touch));
    }
    // ⛔ The continuation's 31 60 is a prefix, not payload: a position split
    // after its MSB position must not read LSB 0x31 / MSB 0x60 (pb 12337).
    {
        Assembler a; Run r;
        const Bytes full = pos(4, 0x22, 0x11);
        feedInto(a, {0x31, 0x60, full[0], full[1], full[2], full[3]}, r);
        EXPECT(r.frames.empty());
        feedInto(a, {0x31, 0x60, full[4], full[5], full[6]}, r);
        EXPECT(r.frames.size() == 1 && same(r.frames[0], full));
        EXPECT(r.badck == 0);
    }
    // A packet that is only the prefix keeps the residual waiting.
    {
        Assembler a; Run r;
        const Bytes full = pos(7, 0x48, 0x3c);
        feedInto(a, {0x31, 0x60, full[0], full[1], full[2], full[3], full[4]}, r);
        feedInto(a, {0x31, 0x60}, r);
        EXPECT(r.frames.empty());
        EXPECT(a.residual.size() == 5);
        feedInto(a, {0x31, 0x60, full[5], full[6]}, r);
        EXPECT(r.frames.size() == 1 && same(r.frames[0], full));
    }
    // A position frame with a wrong sum is not a fader move: resync, and the
    // next good frame still comes through.
    {
        Assembler a; Run r;
        Bytes bad = pos(1, 0x10, 0x20); bad[6] ^= 0x01;
        Bytes p = bad; p.insert(p.end(), touch.begin(), touch.end());
        feedInto(a, p, r);
        EXPECT(r.badck == 1);
        EXPECT(r.frames.size() == 1 && same(r.frames[0], touch));
    }
    // An unknown command after FF is skipped one byte at a time.
    {
        Assembler a; Run r;
        Bytes p = {0xFF, 0x77, 0x01}; p.insert(p.end(), vpot.begin(), vpot.end());
        feedInto(a, p, r);
        EXPECT(r.unknown == 1);
        EXPECT(r.frames.size() == 1 && same(r.frames[0], vpot));
    }
    // A lone FF at the end is a stray byte, not a frame start: not carried.
    {
        Assembler a; Run r;
        Bytes p = touch; p.push_back(0xFF);
        feedInto(a, p, r);
        EXPECT(r.frames.size() == 1);
        EXPECT(a.residual.empty());
    }
    // FF + an unknown command at the end: not carried either. FF + a known one is.
    {
        Assembler a; Run r;
        feedInto(a, {0xFF, 0x77}, r);
        EXPECT(a.residual.empty());
        feedInto(a, {0xFF, 0x22}, r);
        EXPECT(a.residual.size() == 2);
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("uf8_frames: all passed\n");
    return 0;
}
