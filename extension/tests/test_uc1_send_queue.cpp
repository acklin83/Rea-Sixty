// The UC1's outgoing queue: LED writes for one cell collapse to the newest,
// everything else goes out untouched and in order (Uc1SendQueue.h).
// Frank 2026-10-16.
#include "Uc1SendQueue.h"

#include <cstdio>

static int g_fail = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static std::vector<uint8_t> led(uint8_t bank, uint8_t cell, uint8_t b5, uint8_t state)
{
    return { 0xFF, 0x13, 0x04, bank, cell, b5, state, 0x00 };
}

int main()
{
    // Two writes to one cell: only the newest goes out.
    {
        Uc1SendQueue q;
        q.push(led(0x02, 0x50, 0x01, 0x00));
        q.push(led(0x02, 0x50, 0x01, 0xFF));
        const auto out = q.takeAll();
        EXPECT(out.size() == 1);
        EXPECT(out.size() == 1 && out[0][6] == 0xFF);
        EXPECT(q.takeSuperseded() == 1);
        EXPECT(q.takeSuperseded() == 0);
        EXPECT(q.empty());
    }
    // Bank and role byte are part of the cell: these four are four cells.
    {
        Uc1SendQueue q;
        q.push(led(0x01, 0x50, 0x00, 0x01));
        q.push(led(0x02, 0x50, 0x00, 0xFF));
        q.push(led(0x01, 0x50, 0x01, 0x01));
        q.push(led(0x02, 0x51, 0x00, 0xFF));
        EXPECT(q.takeAll().size() == 4);
        EXPECT(q.takeSuperseded() == 0);
    }
    // The newest writes keep their own order: a pair sent 02-then-01 after an
    // older 01-then-02 arrives as 02, 01.
    {
        Uc1SendQueue q;
        q.push(led(0x01, 0x9F, 0x01, 0x01));
        q.push(led(0x02, 0x9F, 0x01, 0x00));
        q.push(led(0x02, 0x9F, 0x01, 0xFF));
        q.push(led(0x01, 0x9F, 0x01, 0x04));
        const auto out = q.takeAll();
        EXPECT(out.size() == 2);
        EXPECT(out.size() == 2 && out[0][3] == 0x02 && out[0][6] == 0xFF);
        EXPECT(out.size() == 2 && out[1][3] == 0x01 && out[1][6] == 0x04);
    }
    // Anything that is not an 8-byte LED write is never touched, even when two
    // are identical, and keeps its place among the LED writes.
    {
        Uc1SendQueue q;
        const std::vector<uint8_t> text{ 0xFF, 0x66, 0x02, 0x03, 0x41, 0xAC };
        q.push(text);
        q.push(led(0x02, 0x10, 0x00, 0x00));
        q.push(text);
        q.push(led(0x02, 0x10, 0x00, 0xFF));
        const auto out = q.takeAll();
        EXPECT(out.size() == 3);
        EXPECT(out.size() == 3 && out[0] == text && out[1] == text);
        EXPECT(out.size() == 3 && out[2][6] == 0xFF);
    }
    // After takeAll the slate is clean: a write for the same cell is new again.
    {
        Uc1SendQueue q;
        q.push(led(0x02, 0x20, 0x00, 0x00));
        (void)q.takeAll();
        q.push(led(0x02, 0x20, 0x00, 0xFF));
        EXPECT(q.takeAll().size() == 1);
        EXPECT(q.takeSuperseded() == 0);
    }
    // A full repaint 15 times over collapses to one repaint.
    {
        Uc1SendQueue q;
        for (int rep = 0; rep < 15; ++rep)
            for (int c = 0; c < 64; ++c)
                q.push(led(0x02, static_cast<uint8_t>(c), 0x00, static_cast<uint8_t>(rep)));
        const auto out = q.takeAll();
        EXPECT(out.size() == 64);
        EXPECT(out.size() == 64 && out[0][6] == 14);
        EXPECT(q.takeSuperseded() == 64 * 14);
    }
    if (g_fail == 0) std::printf("test_uc1_send_queue: all passed\n");
    return g_fail == 0 ? 0 : 1;
}
