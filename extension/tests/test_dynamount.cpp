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

        // Calibration mapping: fader 0..1 → [hMin..hMax]; rotation clamps; faderNorm round-trips.
        Calibration cal; cal.hMin = 10; cal.hMax = 90; cal.vMin = 0; cal.vMax = 100; cal.valid = true;
        mgr.setCalibration(0, cal);
        mgr.setDistance(0, 0.0);   EXPECT(mgr.targetH(0) == 10);
        mgr.setDistance(0, 1.0);   EXPECT(mgr.targetH(0) == 90);
        mgr.setDistance(0, 0.5);   EXPECT(mgr.targetH(0) == 50);
        mgr.setHorizontal(0, 0.25); EXPECT(mgr.targetV(0) == 25);

        // Rotation nudge clamps 0..180.
        mgr.nudgeRotation(0, 1000); EXPECT(mgr.targetR(0) == 180);
        mgr.nudgeRotation(0, -1000); EXPECT(mgr.targetR(0) == 0);

        // faderNorm reflects the active axis.
        mgr.setDistance(0, 0.5);
        EXPECT(mgr.faderNorm(0, /*flipped=*/false) > 0.49 && mgr.faderNorm(0, false) < 0.51);

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

    std::printf("test_dynamount: all passed\n");
    return 0;
}
