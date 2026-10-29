// ModeSync.h: Hue, DynaMount and OBS kept in step between ORC and Rea-Sixty at
// the handover (Frank 29.10.2026). Pinned: the lines, who wins, the stamps.

#include "ModeSync.h"
#include "OrcHandover.h"

#include <cstdio>
#include <map>
#include <string>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

int main()
{
    using namespace modesync;

    // ── the lines: a value may hold tabs, never a newline ─────────────────────
    {
        const std::vector<Value> out = { { "hue_config", 1700, "V1;ip\t10.0.0.2\tabc" },
                                         { "obs_key", 0, "secret" },
                                         { "dynamount_fill", 5, "bad\nvalue" } };
        const auto in = parse(format(out));
        EXPECT(in.size() == 2);
        EXPECT(in[0].key == "hue_config" && in[0].stamp == 1700 && in[0].value == "V1;ip\t10.0.0.2\tabc");
        EXPECT(in[1].key == "obs_key" && in[1].stamp == 0 && in[1].value == "secret");
    }
    // ── who wins ──────────────────────────────────────────────────────────────
    {
        EXPECT(takeIncoming(100, 200, "x", false));           // newer
        EXPECT(!takeIncoming(200, 100, "x", true));           // older, even from ORC
        EXPECT(takeIncoming(0, 0, "x", true));                // no stamps: ORC's
        EXPECT(!takeIncoming(0, 0, "x", false));              // no stamps: not Rea-Sixty's
        EXPECT(!takeIncoming(0, 300, "", true));              // empty never wipes
        EXPECT(!takeIncoming(300, 300, "x", true));           // the same change
    }
    // ── the stamper: a change is stamped, what was there at start is not ──────
    {
        std::map<std::string, std::string> store = { { "hue_config", "A" } };
        std::map<std::string, Ms> stamps;
        auto get = [&](const char* k) { auto it = store.find(k); return it == store.end() ? std::string() : it->second; };
        auto set = [&](const char* k, Ms s) { stamps[k] = s; };
        Stamper st;
        st.prime(get);
        st.tick(get, set, 10);
        EXPECT(stamps.empty());
        store["hue_config"] = "B";
        st.tick(get, set, 20);
        EXPECT(stamps["hue_config"] == 20);
        store["dynamount_presets"] = "P";
        st.tick(get, set, 30);
        EXPECT(stamps["dynamount_presets"] == 30 && stamps["hue_config"] == 20);
        // collect: every key with a value, with its stamp
        const auto vs = collect(get, [&](const char* k) { return stamps.count(k) ? stamps[k] : 0; });
        EXPECT(vs.size() == 2);
    }
    // ── in the handover files, below what older readers read ─────────────────
    {
        const std::string modes = format({ { "hue_key", 42, "k" } });
        const auto r = orc_handover::parseReleased(orc_handover::formatReleased(77, "10.0.0.5,1,90,50;", modes));
        EXPECT(r.pid == 77 && r.positions == "10.0.0.5,1,90,50;");
        // No stands: the positions line is empty, not the first value.
        const auto r2 = orc_handover::parseReleased(orc_handover::formatReleased(77, "", modes));
        EXPECT(r2.positions.empty());
        const std::string back = orc_handover::formatReturned("10.0.0.5,2,90,50;", modes);
        EXPECT(orc_handover::returnedPositions(back) == "10.0.0.5,2,90,50;");
        const auto vs = parse(back);
        EXPECT(vs.size() == 1 && vs[0].key == "hue_key" && vs[0].stamp == 42);
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("mode_sync: all passed\n");
    return 0;
}
