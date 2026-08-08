//
// Round-trip tests for the v11 UF1 plugin-mode map in UserPluginCatalog.
//
// The point of these is the SILENT-DATA-LOSS class of bug this format has hit
// before: the 2026-06-15 emitSlotCache defect wiped every modifier overlay
// because the writer and the parser were asymmetric, and the load filter drops
// any map it considers meaningless. v11 adds both a new block AND a new leg to
// that filter, so both get pinned here.
//
// Build: part of the reaper_uf8 CMake project (test_user_catalog_uf1 target).
//

// parse_ / serialize_ are file-local (anonymous namespace), so the translation
// unit is pulled in whole rather than widening their linkage just for a test.
// REAPERAPI_IMPLEMENT defines the API function pointers in this TU — nothing
// here calls them (only parse_/serialize_ are exercised), they just have to
// resolve at link time.
#define REAPERAPI_IMPLEMENT
#include "UserPluginCatalog.cpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#define EXPECT(cond) do {                                              \
    if (!(cond)) {                                                     \
        std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__,   \
                     #cond);                                           \
        std::exit(1);                                                  \
    }                                                                  \
} while(0)

using namespace uf8;
using namespace uf8::user_plugins;

// A v10 catalog: one CS map, one slot, NO uf1 block and no uf1Mode key.
static const char* kV10 = R"({
  "format_version": 10,
  "plugins": [
    {
      "match": "TestPlug",
      "domain": "ChannelStrip",
      "displayShort": "TEST",
      "isDefault": false,
      "uf8Mode": false,
      "slots": [
        { "linkIdx": 1, "vst3Param": 7, "inverted": false }
      ]
    }
  ]
})";

int main()
{
    // --- v10 loads unchanged, and stays UF1-free through a round trip -------
    {
        UserPluginCatalog c{};
        EXPECT(parse_(kV10, c));
        EXPECT(c.maps.size() == 1);
        const auto& m = c.maps[0];
        EXPECT(m.match == "TestPlug");
        EXPECT(m.slots.size() == 1);
        EXPECT(m.slots[0].vst3Param == 7);
        // The whole safety property of phase 1: no UF1 map => the runtime keeps
        // filling the UF1 sequentially, exactly as before v11.
        EXPECT(m.uf1Mode == false);
        EXPECT(uf1MapHasContent(m.uf1) == false);

        // Serialising must NOT invent a uf1 block for a map that has none.
        const std::string out = serialize_(c);
        EXPECT(out.find("\"uf1\"") == std::string::npos);
        EXPECT(out.find("uf1Mode") == std::string::npos);

        UserPluginCatalog back{};
        EXPECT(parse_(out, back));
        EXPECT(back.maps.size() == 1);
        EXPECT(back.maps[0].slots.size() == 1);
        EXPECT(back.maps[0].uf1Mode == false);
    }

    // --- a UF1 map survives the round trip, sparsely ------------------------
    {
        UserPluginCatalog c{};
        EXPECT(parse_(kV10, c));
        auto& m = c.maps[0];
        m.uf1Mode = true;
        UserUf1Slot a{}; a.pos = 0;  a.vst3Param = 11;
        UserUf1Slot b{}; b.pos = 7;  b.vst3Param = 12;   // sparse: 1..6 unmapped
        b.customLabel = "Drive";
        b.inverted    = true;
        b.sensitivity = 0.25f;
        UserUf1Slot k{}; k.pos = 2;  k.vst3Param = 13;   // soft-key stream
        m.uf1.vpots.push_back(a);
        m.uf1.vpots.push_back(b);
        m.uf1.softKeys.push_back(k);
        EXPECT(uf1MapHasContent(m.uf1));
        // Page count follows the HIGHEST mapped position, both streams.
        EXPECT(uf1MapPageCount(m.uf1) == 2);            // pos 7 => page 1 => 2 pages

        UserPluginCatalog back{};
        EXPECT(parse_(serialize_(c), back));
        EXPECT(back.maps.size() == 1);
        const auto& r = back.maps[0];
        EXPECT(r.uf1Mode == true);
        EXPECT(r.uf1.vpots.size() == 2);
        EXPECT(r.uf1.softKeys.size() == 1);
        // Sparse positions are keyed, not implied by array order.
        const UserUf1Slot* s0 = uf1SlotAt(r.uf1.vpots, 0);
        const UserUf1Slot* s7 = uf1SlotAt(r.uf1.vpots, 7);
        EXPECT(s0 && s0->vst3Param == 11);
        EXPECT(s7 && s7->vst3Param == 12);
        EXPECT(uf1SlotAt(r.uf1.vpots, 3) == nullptr);   // never mapped, stays absent
        // The inherited SlotLayer tuning fields must survive too — that is the
        // exact asymmetry that ate the modifier overlays in June.
        EXPECT(s7->customLabel == "Drive");
        EXPECT(s7->inverted == true);
        EXPECT(s7->sensitivity > 0.24f && s7->sensitivity < 0.26f);
        const UserUf1Slot* k2 = uf1SlotAt(r.uf1.softKeys, 2);
        EXPECT(k2 && k2->vst3Param == 13);
        EXPECT(uf1MapPageCount(r.uf1) == 2);
    }

    // --- ⚠ UF1-ONLY maps must NOT be dropped by the load filter -------------
    // domain=None + uf8Mode=false was "meaningless" before v11 and got silently
    // discarded. With uf1Mode it is a valid map; miss this and every UF1-only
    // map disappears on the next save. The exchange server mirrors the same
    // rule (server/src/lib/rea60map.js) and must stay in step.
    {
        UserPluginCatalog c{};
        EXPECT(parse_(kV10, c));
        auto& m = c.maps[0];
        m.domain  = Domain::None;
        m.uf8Mode = false;
        m.uf1Mode = true;
        m.slots.clear();
        UserUf1Slot a{}; a.pos = 0; a.vst3Param = 5;
        m.uf1.vpots.push_back(a);

        UserPluginCatalog back{};
        EXPECT(parse_(serialize_(c), back));
        EXPECT(back.maps.size() == 1);                  // ← survived the filter
        EXPECT(back.maps[0].domain == Domain::None);
        EXPECT(back.maps[0].uf1Mode == true);
        EXPECT(uf1SlotAt(back.maps[0].uf1.vpots, 0) != nullptr);
        EXPECT(std::string(surfaceScope(back.maps[0])) == "uf1");
    }

    // --- and a truly empty map IS still dropped -----------------------------
    {
        UserPluginCatalog c{};
        EXPECT(parse_(kV10, c));
        auto& m = c.maps[0];
        m.domain  = Domain::None;
        m.uf8Mode = false;
        m.uf1Mode = false;
        m.slots.clear();

        UserPluginCatalog back{};
        EXPECT(parse_(serialize_(c), back));
        EXPECT(back.maps.empty());
    }

    // --- surface taxonomy strings (the exchange's `surfaces` value) ---------
    {
        UserPluginMap m{};
        m.domain = Domain::ChannelStrip;
        EXPECT(std::string(surfaceScope(m)) == "uc1");
        m.uf1Mode = true;
        EXPECT(std::string(surfaceScope(m)) == "uc1+uf1");
        m.uf8Mode = true;
        EXPECT(std::string(surfaceScope(m)) == "uc1+uf8+uf1");
        m.uf1Mode = false;
        EXPECT(std::string(surfaceScope(m)) == "uc1+uf8");
    }

    std::printf("test_user_catalog_uf1: all passed\n");
    return 0;
}
