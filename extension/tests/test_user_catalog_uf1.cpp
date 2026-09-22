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
#include "GrCalibration.h"      // kBcVuBpDb + applyGrCalibration (v18 case)

// seedUf1FromSlots splits the UC1 slots by uc1::linkIdxIsButton. Linking the
// real one drags in most of the UC1 stack, and a unit test wants a rule it
// controls anyway — so: odd linkIdx counts as a button here. The test asserts
// the SPLIT and the ORDER, which is the seed's actual contract.
namespace uc1 {
bool linkIdxIsButton(int linkIdx, bool /*busComp*/) { return (linkIdx % 2) == 1; }
}

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
        // Two, not one: kV10 is a Channel Strip with a FaderLevel slot and an
        // explicit UF1 layer, so the v17 migration writes the PLUG-IN key in.
        // Its own case is below; here it only has to not disturb the rest.
        EXPECT(r.uf1.softKeys.size() == 2);
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

    // --- v12: per-soft-key LED colour round-trips, and 0 stays ABSENT --------
    // The writer emits "ledRgb" only when set, so a map without colours must
    // serialise exactly like a v11 one — the same writer/parser asymmetry that
    // silently wiped the overlays in June is the trap this pins down.
    {
        UserPluginCatalog c{};
        EXPECT(parse_(kV10, c));
        auto& m = c.maps[0];
        m.uf1Mode = true;
        // Current-version document: what this case pins is writer/parser
        // symmetry, and a MIGRATION legitimately breaks byte-idempotence (v17
        // adds the PLUG-IN key to an old CS map). Only an out-of-date document
        // gets migrated, so stamp this one as current and keep the two concerns
        // apart — the v17 shift has its own case at the end.
        c.formatVersion = kCurrentFormatVersion;
        UserUf1Slot lit{};  lit.pos = 0; lit.vst3Param = 21; lit.ledRgb = 0xFF8000;
        UserUf1Slot dark{}; dark.pos = 1; dark.vst3Param = 22;   // no colour set
        m.uf1.softKeys.push_back(lit);
        m.uf1.softKeys.push_back(dark);

        const std::string out = serialize_(c);
        EXPECT(out.find("\"ledRgb\": 16744448") != std::string::npos);  // 0xFF8000
        // Exactly ONE ledRgb key: the uncoloured key must not emit a 0.
        EXPECT(out.find("ledRgb", out.find("ledRgb") + 1) == std::string::npos);

        UserPluginCatalog back{};
        EXPECT(parse_(out, back));
        const auto& r = back.maps[0];
        const UserUf1Slot* a2 = uf1SlotAt(r.uf1.softKeys, 0);
        const UserUf1Slot* b2 = uf1SlotAt(r.uf1.softKeys, 1);
        EXPECT(a2 && a2->ledRgb == 0xFF8000u);
        EXPECT(b2 && b2->ledRgb == 0u);
        // Idempotent: a second pass produces the identical document.
        EXPECT(serialize_(back) == out);
    }

    // --- v13: ONE shared name per parameter, read by all three surfaces ------
    // Sparse + additive: a map that names nothing must serialise exactly like a
    // v12 one, and setParamLabel("") must REMOVE the entry rather than store an
    // empty string (an empty label would shadow the canonical name forever).
    {
        UserPluginCatalog c{};
        EXPECT(parse_(kV10, c));
        auto& m = c.maps[0];
        EXPECT(serialize_(c).find("paramLabels") == std::string::npos);

        EXPECT(setParamLabel(m, 21, "Gate Thr"));
        EXPECT(setParamLabel(m, 27, "Comp Thr"));
        EXPECT(!setParamLabel(m, 21, "Gate Thr"));      // no-op stays a no-op
        const std::string out = serialize_(c);
        EXPECT(out.find("\"paramLabels\"") != std::string::npos);

        UserPluginCatalog back{};
        EXPECT(parse_(out, back));
        EXPECT(back.maps[0].paramLabels.size() == 2);
        EXPECT(serialize_(back) == out);                 // idempotent

        // Clearing removes the entry entirely.
        auto& m2 = back.maps[0];
        EXPECT(setParamLabel(m2, 21, ""));
        EXPECT(m2.paramLabels.size() == 1);
        EXPECT(m2.paramLabels[0].vst3Param == 27);
        EXPECT(!setParamLabel(m2, 999, ""));             // clearing an unset one
    }

    // --- a v11 file (uf1 block, no ledRgb) still loads, colour defaults to 0 --
    {
        UserPluginCatalog c{};
        EXPECT(parse_(kV10, c));
        auto& m = c.maps[0];
        m.uf1Mode = true;
        UserUf1Slot k{}; k.pos = 0; k.vst3Param = 31;
        m.uf1.softKeys.push_back(k);
        std::string v11 = serialize_(c);
        // Whatever version the writer stamps, the READER must not require v12.
        EXPECT(v11.find("ledRgb") == std::string::npos);
        UserPluginCatalog back{};
        EXPECT(parse_(v11, back));
        const UserUf1Slot* k2 = uf1SlotAt(back.maps[0].uf1.softKeys, 0);
        EXPECT(k2 && k2->vst3Param == 31 && k2->ledRgb == 0u);
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

    // --- seeding: enabling the layer must not lose the automatic layout ------
    // This is what makes implicit creation safe. Enabling on a blank grid would
    // trade the working sequential layout for nothing.
    {
        UserPluginCatalog c{};
        EXPECT(parse_(kV10, c));
        auto& m = c.maps[0];
        m.slots.clear();
        auto add = [&](int linkIdx, int param) {
            UserLinkSlot s{}; s.linkIdx = linkIdx; s.vst3Param = param;
            m.slots.push_back(s);
        };
        add(6, 60);   // even → V-Pot stream (per the stub above). LPF  → flat 7
        add(2, 20);   //                                            Width → flat 0
        add(3, 30);   // odd  → soft-key stream
        add(9, 90);
        add(4, -1);   // unmapped → must be skipped entirely
        seedUf1FromSlots(m);

        // ⛔ V-POTS SIT ON THEIR FACTORY POSITION, not packed from 0. linkIdx 2
        // is Width (page 1, V-Pot 1 → flat 0) and linkIdx 6 is the Low Pass
        // (page 2, V-Pot 4 → flat 7), so the gap between them is the factory's
        // own gap. Soft-keys 3 and 9 are on no factory soft-key page, so they
        // are packed AFTER the eight pages (flat 32, 33), not from 0.
        EXPECT(m.uf1.vpots.size() == 2);
        EXPECT(m.uf1.softKeys.size() == 2);
        EXPECT(uf1SlotAt(m.uf1.vpots, 0)->vst3Param == 20);   // Width
        EXPECT(uf1SlotAt(m.uf1.vpots, 7)->vst3Param == 60);   // Low Pass
        EXPECT(uf1SlotAt(m.uf1.vpots, 1) == nullptr);         // factory blank
        EXPECT(uf1SlotAt(m.uf1.softKeys, 32)->vst3Param == 30);
        EXPECT(uf1SlotAt(m.uf1.softKeys, 33)->vst3Param == 90);

        // ⛔ THE BUG THIS ORDER EXISTS FOR. SSL numbers the EQ from the top
        // down, so sorting by linkIdx put HF Freq (10) BEFORE LF Gain (20) and
        // the learned strip showed its EQ upside down against every factory one
        // (Frank 2026-09-18). On the factory pages LF Gain is page 3 (flat 8)
        // and HF Freq is page 6 (flat 21): low band first, as the panel reads.
        {
            UserPluginCatalog c2{};
            EXPECT(parse_(kV10, c2));
            auto& m2 = c2.maps[0];
            m2.slots.clear();
            auto add2 = [&](int linkIdx, int param) {
                UserLinkSlot s{}; s.linkIdx = linkIdx; s.vst3Param = param;
                m2.slots.push_back(s);
            };
            add2(10, 100);   // HF Freq  — lower linkIdx, must NOT come first
            add2(20, 200);   // LF Gain  — higher linkIdx, must come first
            seedUf1FromSlots(m2);
            EXPECT(uf1SlotAt(m2.uf1.vpots,  8)->vst3Param == 200);  // LF Gain
            EXPECT(uf1SlotAt(m2.uf1.vpots, 21)->vst3Param == 100);  // HF Freq
        }

        // The Bus Comp has its own two-page factory layout and its own linkIdx
        // namespace, so it needs its own guard: Release (4) sits on page 1 while
        // Makeup (2) sits on page 2, i.e. the higher link index comes FIRST.
        // Sorting by linkIdx would swap them.
        {
            UserPluginCatalog c3{};
            EXPECT(parse_(kV10, c3));
            auto& m3 = c3.maps[0];
            m3.domain = Domain::BusComp;
            m3.slots.clear();
            auto add3 = [&](int linkIdx, int param) {
                UserLinkSlot s{}; s.linkIdx = linkIdx; s.vst3Param = param;
                m3.slots.push_back(s);
            };
            add3(2, 22);   // Makeup  → page 2, flat 4
            add3(4, 44);   // Release → page 1, flat 3
            seedUf1FromSlots(m3);
            EXPECT(uf1SlotAt(m3.uf1.vpots, 3)->vst3Param == 44);   // Release
            EXPECT(uf1SlotAt(m3.uf1.vpots, 4)->vst3Param == 22);   // Makeup
        }

        // Idempotent: seeding a populated map must leave it alone, else enabling
        // twice (or a later auto-enable) would stomp the user's own edits.
        m.uf1.vpots[0].vst3Param = 999;
        seedUf1FromSlots(m);
        EXPECT(uf1SlotAt(m.uf1.vpots, 0)->vst3Param == 999);
    }

    // --- seeding a CS with a fader: the PLUG-IN key takes soft-key 4 ---------
    // The runtime reserves kUf1LearnedStripKeyPos in the packed soft-key stream
    // for a CS that has a FaderLevel slot. The seed has to place the same key,
    // or switching the explicit layer on would shift every soft-key back by one
    // and lose it — the exact visible change this seeder exists to prevent.
    {
        UserPluginCatalog c{};
        EXPECT(parse_(kV10, c));
        auto& m = c.maps[0];              // domain ChannelStrip
        m.slots.clear();
        auto add = [&](int linkIdx, int param) {
            UserLinkSlot s{}; s.linkIdx = linkIdx; s.vst3Param = param;
            m.slots.push_back(s);
        };
        add(1, 10);   // FaderLevel → this is what earns the key
        add(3, 30);   // odd → soft-key stream (per the stub above)
        add(5, 50);
        add(7, 70);
        add(9, 90);
        EXPECT(uf1MapWantsStripKey(m));
        seedUf1FromSlots(m);

        // Factory places: Polarity (5) is page 1 key 1, the key is page 1
        // key 4, and 1/3/7/9 are on no factory soft-key page, so they follow
        // the eight pages in linkIdx order. Nothing moved to make room.
        EXPECT(uf1SlotAt(m.uf1.softKeys, 0)->vst3Param == 50);
        EXPECT(uf1SlotAt(m.uf1.softKeys, 1) == nullptr);
        const UserUf1Slot* key = uf1SlotAt(m.uf1.softKeys, 3);
        EXPECT(key && key->special == uint8_t(Uf1SkSpecial::StripMode));
        EXPECT(key->vst3Param < 0);       // it is an action, not a param
        EXPECT(uf1SlotAt(m.uf1.softKeys, 32)->vst3Param == 10);
        EXPECT(uf1SlotAt(m.uf1.softKeys, 33)->vst3Param == 30);
        EXPECT(uf1SlotAt(m.uf1.softKeys, 34)->vst3Param == 70);
        EXPECT(uf1SlotAt(m.uf1.softKeys, 35)->vst3Param == 90);

        // …and it survives a round trip, or the key would vanish on restart.
        c.maps[0] = m;
        UserPluginCatalog back{};
        EXPECT(parse_(serialize_(c).c_str(), back));
        const UserUf1Slot* k2 = uf1SlotAt(back.maps[0].uf1.softKeys, 3);
        EXPECT(k2 && k2->special == uint8_t(Uf1SkSpecial::StripMode));

        // No FaderLevel slot → no key, and nothing shifts.
        UserPluginCatalog c2{};
        EXPECT(parse_(kV10, c2));
        auto& m2 = c2.maps[0];
        m2.slots.clear();
        UserLinkSlot only{}; only.linkIdx = 3; only.vst3Param = 30;
        m2.slots.push_back(only);
        EXPECT(!uf1MapWantsStripKey(m2));
        seedUf1FromSlots(m2);
        EXPECT(m2.uf1.softKeys.size() == 1);
        EXPECT(uf1SlotAt(m2.uf1.softKeys, 32)->vst3Param == 30);
        EXPECT(uf1SlotAt(m2.uf1.softKeys, 3) == nullptr);
    }

    // --- soft-keys land where a FACTORY Channel Strip has them (22.09.) ------
    // Frank: "wieso ist die Verteilung auf dem UF1 nicht GANZ GENAU so wie bei
    // SSL Factory CS?" The CS2 p188 pages, repeats included: EQ In on all four
    // EQ pages (flat 11, 15, 19, 23), LF Bell page 3 key 1 (8), Peak page 7
    // key 2 (25), Expander page 8 key 1 (28). Odd linkIdx = button in the stub.
    {
        UserPluginCatalog c{};
        EXPECT(parse_(kV10, c));
        auto& m = c.maps[0];
        m.slots.clear();
        auto add = [&](int linkIdx, int param) {
            UserLinkSlot s{}; s.linkIdx = linkIdx; s.vst3Param = param;
            m.slots.push_back(s);
        };
        add(15, 150);   // EQ In
        add(21, 210);   // LF Bell
        add(25, 250);   // Comp Peak
        add(33, 330);   // Gate/Exp
        seedUf1FromSlots(m);
        for (int f : { 11, 15, 19, 23 })
            EXPECT(uf1SlotAt(m.uf1.softKeys, f) && uf1SlotAt(m.uf1.softKeys, f)->vst3Param == 150);
        EXPECT(uf1SlotAt(m.uf1.softKeys, 8)->vst3Param  == 210);
        EXPECT(uf1SlotAt(m.uf1.softKeys, 25)->vst3Param == 250);
        EXPECT(uf1SlotAt(m.uf1.softKeys, 28)->vst3Param == 330);
        EXPECT(uf1SlotAt(m.uf1.softKeys, 0) == nullptr);   // no Polarity learned
        EXPECT(uf1SlotAt(m.uf1.softKeys, 10) == nullptr);  // no EQ Type learned
        EXPECT(uf1FactorySoftKeyLinkAt(3, false) == -1);   // PLUG-IN's place
        EXPECT(uf1FactorySoftKeyPositionCount(true) == 0); // Bus Comp: packed
    }

    // --- v17: the PLUG-IN key is written into an EXISTING explicit map ------
    // The runtime reserves the position in the packed stream, but a map with an
    // explicit UF1 layer has no stream — and withholding the key there turned
    // the feature off for exactly the strips someone mapped by hand. So the
    // migration writes it in and shifts what sat at or past the position.
    {
        UserPluginCatalog c{};
        EXPECT(parse_(kV10, c));                 // CS domain, FaderLevel slot
        auto& m = c.maps[0];
        m.uf1Mode = true;
        auto sk = [&](int pos, int param) {
            UserUf1Slot s{}; s.pos = pos; s.vst3Param = param;
            m.uf1.softKeys.push_back(s);
        };
        sk(0, 100); sk(1, 101); sk(2, 102); sk(3, 103); sk(4, 104);

        UserPluginCatalog back{};
        EXPECT(parse_(serialize_(c), back));
        const auto& r = back.maps[0];
        EXPECT(r.uf1.softKeys.size() == 6);
        EXPECT(uf1SlotAt(r.uf1.softKeys, 0)->vst3Param == 100);   // below: put
        EXPECT(uf1SlotAt(r.uf1.softKeys, 2)->vst3Param == 102);
        const UserUf1Slot* key = uf1SlotAt(r.uf1.softKeys, 3);
        EXPECT(key && key->special == uint8_t(Uf1SkSpecial::StripMode));
        EXPECT(uf1SlotAt(r.uf1.softKeys, 4)->vst3Param == 103);   // and past: moved
        EXPECT(uf1SlotAt(r.uf1.softKeys, 5)->vst3Param == 104);

        // Idempotent even before the first save at v17: re-parsing the same
        // old-version payload must not shift a second time, or a user who never
        // saves walks their layout one key further on every launch.
        UserPluginCatalog again{};
        EXPECT(parse_(serialize_(back), again));
        EXPECT(again.maps[0].uf1.softKeys.size() == 6);
        EXPECT(uf1SlotAt(again.maps[0].uf1.softKeys, 4)->vst3Param == 103);

        // A map WITHOUT the explicit layer is left alone: the runtime reserves
        // the position in its packed stream instead, and writing a slot here
        // would switch the layer on behind the user's back.
        UserPluginCatalog c2{};
        EXPECT(parse_(kV10, c2));
        c2.maps[0].uf1Mode = false;
        UserPluginCatalog back2{};
        EXPECT(parse_(serialize_(c2), back2));
        EXPECT(back2.maps[0].uf1.softKeys.empty());
    }

    // --- v18: a captured GR breakpoint round-trips, and moves the curve -----
    // The pair (reading, tick) is what makes a plug-in calibratable when its
    // needle and its host reading disagree. Losing the reading half on a save
    // would silently move every correction back onto the tick.
    {
        UserPluginCatalog c{};
        EXPECT(parse_(kV10, c));
        c.formatVersion = kCurrentFormatVersion;
        auto& m = c.maps[0];
        // "when it reports 2.2, show 4" — Frank's bx_townhouse case.
        m.metering.grBcVuRawDb[1] = 2.2;
        m.metering.grBcVuCalDb[1] = 4.0 - 2.2;

        UserPluginCatalog back{};
        EXPECT(parse_(serialize_(c), back));
        const auto& r = back.maps[0].metering;
        EXPECT(r.grBcVuRawDb[1] > 2.19 && r.grBcVuRawDb[1] < 2.21);
        EXPECT(r.grBcVuCalDb[1] > 1.79 && r.grBcVuCalDb[1] < 1.81);
        EXPECT(r.grBcVuRawDb[0] < 0.0);          // untouched stays "at the tick"

        // The effective scale puts the correction where it was measured. The
        // uncaptured ticks FOLLOW it rather than staying at themselves — the
        // pull-along case has its own block below.
        double eff[6];
        grEffectiveBreakpoints(kBcVuBpDb, r.grBcVuRawDb, 6, eff);
        EXPECT(eff[0] == 0.0);
        EXPECT(eff[1] > 2.19 && eff[1] < 2.21);
        EXPECT(eff[2] > 4.39 && eff[2] < 4.41);        // 8 * (2.2/4)
        // …so a reading of 2.2 now displays as 4.0, which is the whole point.
        double effOff[6];
        grResolveCalibration(kBcVuBpDb, r.grBcVuRawDb, r.grBcVuCalDb, 6,
                             eff, effOff);
        const double shown = applyGrCalibration(2.2, eff, effOff, 6);
        EXPECT(shown > 3.99 && shown < 4.01);

        // Out-of-order captures must not break the strictly-increasing
        // precondition of applyGrCalibration.
        double raw2[6] = { -1.0, 9.0, 3.0, -1.0, -1.0, -1.0 };
        double eff2[6];
        grEffectiveBreakpoints(kBcVuBpDb, raw2, 6, eff2);
        for (int i = 1; i < 6; ++i) EXPECT(eff2[i] > eff2[i - 1]);
    }

    // --- v18: ONE capture pulls the whole scale with it ---------------------
    // A plug-in reporting a fraction of what it shows is off by a fraction
    // everywhere, so the ticks nobody captured follow the one that was
    // captured. Without this a single measurement fixed 4 dB and left 8, 12
    // and 20 exactly as wrong as before.
    {
        const double ticks[6] = {0, 4, 8, 12, 16, 20};
        double raw[6] = {-1, 2.2, -1, -1, -1, -1};      // needle 4, reports 2.2
        double off[6] = {0, 0, 0, 0, 0, 0};
        double bp[6], eo[6];
        grResolveCalibration(ticks, raw, off, 6, bp, eo);

        // Slope 2.2/4 = 0.55 through the origin, so 8 sits at 4.4, 20 at 11.
        EXPECT(bp[0] == 0.0);
        EXPECT(bp[1] > 2.19 && bp[1] < 2.21);
        EXPECT(bp[2] > 4.39 && bp[2] < 4.41);
        EXPECT(bp[5] > 10.99 && bp[5] < 11.01);
        // …and each still lands on its own tick when that reading comes in.
        for (int i = 0; i < 6; ++i) {
            const double shown = applyGrCalibration(bp[i], bp, eo, 6);
            EXPECT(shown > ticks[i] - 0.01 && shown < ticks[i] + 0.01);
        }

        // A second capture higher up is fine-tuning: it REPLACES the guess
        // there, the segment below it interpolates between the two, and above
        // it the new slope extends.
        raw[2] = 5.0;                                   // measured, not 4.4
        grResolveCalibration(ticks, raw, off, 6, bp, eo);
        EXPECT(bp[1] > 2.19 && bp[1] < 2.21);           // untouched
        EXPECT(bp[2] > 4.99 && bp[2] < 5.01);           // measured wins
        // slope of the last segment = (5.0-2.2)/4 = 0.7 → 12 sits at 5.0+2.8
        EXPECT(bp[3] > 7.79 && bp[3] < 7.81);

        // Nothing captured = the legacy table, offsets typed at the ticks.
        double none[6] = {-1, -1, -1, -1, -1, -1};
        double typed[6] = {0, 4.1, 4.7, 4.2, 0, 0};     // Frank's townhouse row
        grResolveCalibration(ticks, none, typed, 6, bp, eo);
        for (int i = 0; i < 6; ++i) {
            EXPECT(bp[i] == ticks[i]);
            EXPECT(eo[i] == typed[i]);
        }
    }

    std::printf("test_user_catalog_uf1: all passed\n");
    return 0;
}
