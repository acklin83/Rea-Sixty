// Uf8Fader.h: a UF8 position frame read, and the motor driven back (docs/orc-
// uf8-plan.md stage 5). The numbers are the ones measured on 2026-10-25.

#include "Uf8Fader.h"

#include <cstdio>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

using namespace uf8fader;

int main()
{
    // The stops, both streams.
    EXPECT(stop(130, true) == -1);           // bottom, bit 7: 128..142 measured
    EXPECT(stop(300, false) == -1);          // bottom, no bit 7: wider zone
    EXPECT(stop(300, true) == 0);
    EXPECT(stop(15400, true) == +1);         // top, bit 7: 15389..15488
    EXPECT(stop(15400, false) == 0);         // the other stream reads higher
    EXPECT(stop(15550, false) == +1);        // 15546..15615
    EXPECT(stop(8000, true) == 0);

    // A stop is the end itself, from either stream.
    EXPECT(read(130, true, false).pbOut == 0);
    EXPECT(read(15600, false, false).pbOut == kPbMax);
    EXPECT(read(15600, false, false).usable);   // a stop is usable without a grab

    // The stream without bit 7 reads ~70 higher: shifted down.
    {
        const Read r = read(8070, false, true);
        EXPECT(r.pbIn == 8000 && r.pbOut == 8000 && r.usable);
    }
    // ...and before a grab it is not used (no offset known yet).
    EXPECT(!read(8070, false, false).usable);
    EXPECT(read(8000, true, false).usable);
    EXPECT(read(8000, true, false).pbOut == 8000);
    EXPECT(read(40, false, true).pbIn == 0);    // no wrap below zero

    // Echo: bit 7 set on the LSB.
    {
        const auto f = echo(3, 0x1234 & 0x3FFF);
        EXPECT(f == uf8::buildFaderPosition(3, static_cast<std::uint8_t>((0x1234 & 0x7F) | 0x80),
                                            static_cast<std::uint8_t>((0x1234 >> 7) & 0x7F)));
    }
    EXPECT(limp(5) == uf8::buildMotorEnable(5, false));

    // Drive: a limp strip takes the bit-7 echo, then the enable, then the value.
    {
        bool engaged = false;
        const auto f = drive(2, 9000, engaged);
        EXPECT(engaged);
        EXPECT(f.size() == 3);
        if (f.size() == 3) {
            EXPECT(f[0] == echo(2, 9000));
            EXPECT(f[1] == uf8::buildMotorEnable(2, true));
            EXPECT(f[2] == uf8::buildFaderPosition(2, 9000 & 0x7F, (9000 >> 7) & 0x7F));
        }
        // Engaged: the value alone.
        const auto g = drive(2, 9100, engaged);
        EXPECT(g.size() == 1);
        // Clamped to the top.
        bool e2 = true;
        const auto h = drive(0, 16383, e2);
        EXPECT(h.size() == 1 && h[0] == uf8::buildFaderPosition(0, kPbMax & 0x7F, (kPbMax >> 7) & 0x7F));
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("uf8_fader: all passed\n");
    return 0;
}
