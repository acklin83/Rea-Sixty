//
// Unit tests for the pure-logic pieces of DynaMountClient (query/request
// builders, clamping, response check). No sockets, no REAPER.
//
// Build: part of the reaper_uf8 CMake project (test_dynamount target).
//

#include "DynaMountClient.h"
#include "DynaMountManager.h"

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

int main()
{
    using namespace uf8::dynamount;

    // --- clampi -------------------------------------------------------------
    EXPECT(clampi(50, 0, 100) == 50);
    EXPECT(clampi(-5, 0, 100) == 0);
    EXPECT(clampi(250, 0, 100) == 100);

    // --- gen1MoveQuery: matches the live-captured wire format ---------------
    //   GET /move?h=70&r=94&v=0&s=9   (from packet capture of .253)
    EXPECT(gen1MoveQuery(70, 94, 0, 9) == "h=70&r=94&v=0&s=9");
    EXPECT(gen1MoveQuery(50, 90, 0)    == "h=50&r=90&v=0&s=9");  // default speed 9

    // --- clamping inside the query ------------------------------------------
    // h/v clamp to 0..100, rotation clamps to 0..180 (Frank's choice: clamp).
    EXPECT(gen1MoveQuery(150, 200, -10, 9) == "h=100&r=180&v=0&s=9");

    // --- full HTTP/1.0 request ----------------------------------------------
    std::string req = gen1Request("192.168.177.253", "h=50&r=90&v=0&s=9");
    EXPECT(req.find("GET /move?h=50&r=90&v=0&s=9 HTTP/1.0\r\n") == 0);
    EXPECT(req.find("Host: 192.168.177.253\r\n") != std::string::npos);
    EXPECT(req.find("Connection: close\r\n\r\n") != std::string::npos);

    // --- response success check (live body was {"result": "success"}) -------
    EXPECT(isSuccessBody("{\"result\": \"success\"}"));
    EXPECT(!isSuccessBody("{\"result\": \"error\"}"));
    EXPECT(!isSuccessBody(""));

    // --- proto names --------------------------------------------------------
    EXPECT(std::string(protoName(Proto::Gen1Http)) == "Gen1");
    EXPECT(std::string(protoName(Proto::Offline))  == "Offline");

    // ========================= DynaMountManager ==========================
    {
        DynaMountManager mgr;   // no start() — pure mapping logic, no thread

        // 3 mounts defined in slots 0,2,5 (non-contiguous).
        mgr.setMount(0, true,  "Snare", "192.168.177.253", 3);
        mgr.setMount(2, true,  "OH-L",  "192.168.177.254", 5);
        mgr.setMount(5, true,  "Room",  "192.168.177.250", 7);
        mgr.setMount(1, false, "",      "",                0);

        EXPECT(mgr.definedCount() == 3);

        // Fill from LEFT: pinned strips 0,1,2 → mounts 0,2,5; strips 3..7 = tracks.
        mgr.setFillDir(FillDir::Left);
        EXPECT(mgr.mountForStrip(0) == 0);
        EXPECT(mgr.mountForStrip(1) == 2);
        EXPECT(mgr.mountForStrip(2) == 5);
        EXPECT(mgr.mountForStrip(3) == -1);     // normal track strip
        EXPECT(mgr.isDynaStrip(0) && !mgr.isDynaStrip(7));

        // Fill from RIGHT: pinned strips 5,6,7 → mounts 0,2,5; strips 0..4 = tracks.
        mgr.setFillDir(FillDir::Right);
        EXPECT(mgr.mountForStrip(4) == -1);
        EXPECT(mgr.mountForStrip(5) == 0);
        EXPECT(mgr.mountForStrip(6) == 2);
        EXPECT(mgr.mountForStrip(7) == 5);

        // Calibration mapping. Distance (Y) fader is INVERTED: fader top
        // (f01=1) → nearest = hMin; fader bottom (f01=0) → hMax. Left/right
        // (X/v) stays direct. Rotation clamps; faderNorm round-trips inverted.
        Calibration cal; cal.hMin = 10; cal.hMax = 90; cal.vMin = 0; cal.vMax = 100; cal.valid = true;
        mgr.setCalibration(0, cal);
        mgr.setDistance(0, 1.0);   EXPECT(mgr.targetH(0) == 10);   // top = nearest
        mgr.setDistance(0, 0.0);   EXPECT(mgr.targetH(0) == 90);   // bottom = farthest
        mgr.setDistance(0, 0.5);   EXPECT(mgr.targetH(0) == 50);
        mgr.setHorizontal(0, 0.25); EXPECT(mgr.targetV(0) == 25);

        // Rotation nudge clamps 0..180.
        mgr.nudgeRotation(0, 1000); EXPECT(mgr.targetR(0) == 180);
        mgr.nudgeRotation(0, -1000); EXPECT(mgr.targetR(0) == 0);

        // setTargets sets absolute device units (used by Home + state restore).
        mgr.setTargets(0, /*h*/0, /*r*/90, /*v*/50, false);
        EXPECT(mgr.targetH(0) == 0 && mgr.targetR(0) == 90 && mgr.targetV(0) == 50);

        // faderNorm reflects the active axis; distance is inverted, so the
        // fader sits near the TOP (≈1.0) when the mount is at its nearest (hMin).
        mgr.setDistance(0, 0.5);
        EXPECT(mgr.faderNorm(0, /*flipped=*/false) > 0.49 && mgr.faderNorm(0, false) < 0.51);
        mgr.setTargets(0, /*h*/cal.hMin, 90, 50, false);
        EXPECT(mgr.faderNorm(0, /*flipped=*/false) > 0.99);   // nearest → fader top

        // --- serialize / deserialize round-trip ------------------------------
        std::string blob = mgr.serialize();
        DynaMountManager mgr2;
        mgr2.deserialize(blob);
        EXPECT(mgr2.definedCount() == 3);
        DynaMountManager::Info a = mgr.info(2), b = mgr2.info(2);
        EXPECT(b.enabled && b.name == "OH-L" && b.ip == "192.168.177.254" && b.color == 5);
        DynaMountManager::Info c0 = mgr2.info(0);
        EXPECT(c0.cal.hMin == 10 && c0.cal.hMax == 90 && c0.cal.valid);
        // Disabled slot stays empty after round-trip.
        EXPECT(!mgr2.info(1).enabled);
    }

    // --- presets (Frank 29.10.2026, docs/orc-uf8-plan.md section 4) ---------
    {
        DynaMountManager m;
        m.setMount(0, true, "Kick",  "10.0.0.1", 1);
        m.setMount(3, true, "Snare", "10.0.0.2", 2);
        EXPECT(!m.preset(0).defined());
        EXPECT(!m.recallPreset(0));                       // empty slot
        EXPECT(m.lastPreset() == -1);

        m.setTargets(0, 10, 45, 30, false);
        m.setTargets(3, 70, 120, 80, false);
        m.storePreset(2);
        EXPECT(m.takePresetsDirty());
        EXPECT(m.lastPreset() == 2);
        const auto p = m.preset(2);
        EXPECT(p.defined() && p.pos.size() == 2);
        EXPECT(p.name == "Preset 3");                       // default name

        // Move everything, then recall: back where the preset was.
        m.setTargets(0, 0, 90, 50, false);
        m.setTargets(3, 0, 90, 50, false);
        EXPECT(m.recallPreset(2));
        EXPECT(m.targetH(0) == 10 && m.targetR(0) == 45 && m.targetV(0) == 30);
        EXPECT(m.targetH(3) == 70 && m.targetR(3) == 120 && m.targetV(3) == 80);

        // ⛔ By IP, not by row: the stands swap rows, each still gets its own.
        m.setMount(0, true, "Snare", "10.0.0.2", 2);
        m.setMount(3, true, "Kick",  "10.0.0.1", 1);
        m.setTargets(0, 0, 90, 50, false);
        m.setTargets(3, 0, 90, 50, false);
        EXPECT(m.recallPreset(2));
        EXPECT(m.targetH(0) == 70 && m.targetR(0) == 120);  // Snare's position
        EXPECT(m.targetH(3) == 10 && m.targetR(3) == 45);   // Kick's

        // A stand the preset does not know stays put; one that is off is skipped.
        m.setMount(5, true, "Room", "10.0.0.9", 3);
        m.setTargets(5, 33, 66, 44, false);
        m.setMount(0, false, "Snare", "10.0.0.2", 2);
        m.setTargets(0, 1, 2, 3, false);
        EXPECT(m.recallPreset(2));
        EXPECT(m.targetH(5) == 33 && m.targetR(5) == 66);
        EXPECT(m.targetH(0) == 1);

        // Names are scrubbed of every separator the blob uses.
        m.setPresetName(2, "Dr;ums\t|n,ah");
        EXPECT(m.preset(2).name == "Dr ums  n ah");

        // Round trip, and no newline in it (ExtState cuts at the first one).
        const std::string blob = m.serializePresets();
        EXPECT(blob.find('\n') == std::string::npos);
        DynaMountManager m2;
        m2.deserializePresets(blob);
        const auto q = m2.preset(2);
        EXPECT(q.name == "Dr ums  n ah" && q.pos.size() == 2);
        EXPECT(q.pos[0].ip == "10.0.0.1" && q.pos[0].h == 10 && q.pos[0].r == 45 && q.pos[0].v == 30);
        EXPECT(q.pos[1].ip == "10.0.0.2" && q.pos[1].v == 80);
        EXPECT(m2.serializePresets() == blob);              // stable, loses nothing
        EXPECT(!m2.preset(0).defined());

        // Clear.
        m.clearPreset(2);
        EXPECT(!m.preset(2).defined());
        EXPECT(m.lastPreset() == -1);
    }

    std::printf("test_dynamount: all passed\n");
    return 0;
}
