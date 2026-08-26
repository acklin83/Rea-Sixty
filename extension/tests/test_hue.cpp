//
// Unit tests for the pure-logic pieces of Hue Mode: HueColor (conversions,
// gamut) and HueClient (URLs, request bodies, response parsers). No sockets, no
// bridge, no REAPER.
//
// Build: part of the reaper_uf8 CMake project (test_hue target).
//
// The JSON fixtures below are trimmed CLIP API v2 payloads in the shape the
// bridge actually sends: a {"errors":[…],"data":[…]} envelope around each list,
// mirek reported alongside a mirek_valid flag, a room whose writable id lives in
// its services array rather than on the room itself.
//

#include "HueClient.h"
#include "HueColor.h"

#include <cmath>
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

static bool near(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
}

static bool has(const std::string& hay, const char* needle)
{
    return hay.find(needle) != std::string::npos;
}

int main()
{
    using namespace uf8::hue;

    // ======================= HueColor ======================================

    // --- rgbToXy against the Wide RGB D65 primaries -------------------------
    // Pure red through the published matrix lands on (0.7006, 0.2993) — the
    // corner every Hue gamut-C lamp reports as its red. Getting the sRGB matrix
    // in here by mistake would put it near (0.64, 0.33) instead, which is why
    // this test pins the number rather than a range.
    {
        const Xy red = rgbToXy(1.0, 0.0, 0.0);
        EXPECT(near(red.x, 0.7006, 0.001));
        EXPECT(near(red.y, 0.2993, 0.001));

        const Xy green = rgbToXy(0.0, 1.0, 0.0);
        EXPECT(near(green.x, 0.1724, 0.001));
        EXPECT(near(green.y, 0.7468, 0.001));

        const Xy blue = rgbToXy(0.0, 0.0, 1.0);
        EXPECT(near(blue.x, 0.1355, 0.001));
        EXPECT(near(blue.y, 0.0399, 0.001));

        // White sits on the D65 point, near enough.
        const Xy white = rgbToXy(1.0, 1.0, 1.0);
        EXPECT(near(white.x, 0.3227, 0.005));
        EXPECT(near(white.y, 0.3290, 0.005));

        // Black has no chromaticity at all; it must not divide by zero.
        const Xy black = rgbToXy(0.0, 0.0, 0.0);
        EXPECT(black.x == 0.0 && black.y == 0.0);
    }

    // --- hue angle round trip ------------------------------------------------
    // Not lossless (the header says so), so the tolerance is generous. What it
    // catches is a wrong sector, an inverted axis or a degrees/radians mix-up:
    // those miss by tens of degrees, not by three.
    {
        const double angles[] = { 0.0, 60.0, 120.0, 180.0, 240.0, 300.0, 359.0 };
        for (const double a : angles) {
            const Xy xy = hueSatToXy(a, 1.0);
            double back = -1.0, sat = -1.0;
            xyToHueSat(xy, &back, &sat);
            double delta = std::fabs(back - a);
            if (delta > 180.0) delta = 360.0 - delta;   // wrap at the seam
            EXPECT(delta < 12.0);
            EXPECT(sat > 0.8);
        }

        // Saturation 0 is the white point, whatever the angle says.
        const Xy w1 = hueSatToXy(0.0,   0.0);
        const Xy w2 = hueSatToXy(210.0, 0.0);
        EXPECT(near(w1.x, w2.x, 1e-9) && near(w1.y, w2.y, 1e-9));

        // A negative or over-wound angle is the same colour as its fold.
        const Xy a = hueSatToXy(-90.0, 1.0);
        const Xy b = hueSatToXy(270.0, 1.0);
        EXPECT(near(a.x, b.x, 1e-9) && near(a.y, b.y, 1e-9));
    }

    // --- xyToRgb24 -----------------------------------------------------------
    {
        // Red chromaticity has to come back red-dominant, not merely non-zero.
        const uint32_t rgb = xyToRgb24(rgbToXy(1.0, 0.0, 0.0));
        const int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
        EXPECT(r > 200 && g < 80 && b < 80);

        // y == 0 is not a colour and must not blow up.
        EXPECT(xyToRgb24(Xy{ 0.5, 0.0 }) == 0x000000u);
    }

    // --- gamut ---------------------------------------------------------------
    {
        Gamut gm;
        gm.red   = Xy{ 0.6915, 0.3083 };   // a real gamut C triangle
        gm.green = Xy{ 0.1700, 0.7000 };
        gm.blue  = Xy{ 0.1532, 0.0475 };
        gm.valid = true;

        // The centroid is inside by construction.
        const Xy mid{ (gm.red.x + gm.green.x + gm.blue.x) / 3.0,
                      (gm.red.y + gm.green.y + gm.blue.y) / 3.0 };
        EXPECT(inGamut(mid, gm));
        const Xy same = clampToGamut(mid, gm);
        EXPECT(near(same.x, mid.x, 1e-12) && near(same.y, mid.y, 1e-12));

        // Far outside: clamped back onto the triangle, and the clamp is
        // idempotent — clamping twice must not walk the point along an edge.
        const Xy outside{ 0.95, 0.05 };
        EXPECT(!inGamut(outside, gm));
        const Xy fixed = clampToGamut(outside, gm);
        EXPECT(inGamut(fixed, gm));
        const Xy again = clampToGamut(fixed, gm);
        EXPECT(near(again.x, fixed.x, 1e-9) && near(again.y, fixed.y, 1e-9));

        // No gamut reported → nothing is out of reach and nothing moves.
        Gamut none;
        EXPECT(!none.valid);
        EXPECT(inGamut(outside, none));
        const Xy untouched = clampToGamut(outside, none);
        EXPECT(untouched.x == outside.x && untouched.y == outside.y);
    }

    // --- mireds --------------------------------------------------------------
    {
        EXPECT(mirekFromWarmth(0.0) == kMirekMin);
        EXPECT(mirekFromWarmth(1.0) == kMirekMax);
        EXPECT(mirekFromWarmth(-5.0) == kMirekMin);     // clamped, not wrapped
        EXPECT(mirekFromWarmth(99.0) == kMirekMax);
        EXPECT(near(warmthFromMirek(kMirekMin), 0.0, 1e-12));
        EXPECT(near(warmthFromMirek(kMirekMax), 1.0, 1e-12));

        // Warm end is redder than the cold end. That is the whole claim the
        // colour bar makes about colour temperature.
        const uint32_t cold = mirekToRgb24(kMirekMin);
        const uint32_t warm = mirekToRgb24(kMirekMax);
        EXPECT((int)((warm >> 16) & 0xFF) >= (int)((cold >> 16) & 0xFF));
        EXPECT((int)(warm & 0xFF)         <  (int)(cold & 0xFF));
    }

    // ======================= HueClient: URLs ================================

    EXPECT(discoveryUrl() == "https://discovery.meethue.com/");
    EXPECT(pairUrl("192.168.1.20") == "https://192.168.1.20/api");
    EXPECT(resourceUrl("192.168.1.20", kTypeLight)
           == "https://192.168.1.20/clip/v2/resource/light");
    EXPECT(resourceUrl("192.168.1.20", kTypeScene, "abc-123")
           == "https://192.168.1.20/clip/v2/resource/scene/abc-123");
    EXPECT(eventStreamUrl("192.168.1.20")
           == "https://192.168.1.20/eventstream/clip/v2");
    EXPECT(appKeyHeader("KEY") == "hue-application-key: KEY");

    EXPECT(std::string(typeForKind(TargetKind::Light)) == "light");
    EXPECT(std::string(typeForKind(TargetKind::Group)) == "grouped_light");
    EXPECT(minGapMsForKind(TargetKind::Light) == 100);
    EXPECT(minGapMsForKind(TargetKind::Group) == 1000);

    // ======================= HueClient: bodies ==============================

    {
        const std::string b = pairBody("rea-sixty", "studio-mac");
        EXPECT(has(b, "\"devicetype\":\"rea-sixty#studio-mac\""));
        EXPECT(has(b, "\"generateclientkey\":true"));
    }

    {
        // Off is on:false and carries nothing else.
        LightWrite w;
        w.setOn = true; w.on = false;
        const std::string b = lightBody(w);
        EXPECT(b == "{\"on\":{\"on\":false}}");
    }

    {
        // ⛔ brightness 0 must never reach the wire — the bridge reads it as
        // "lowest possible", so a fader at the bottom would glow instead of
        // going out. The floor is 1 and off is a separate field.
        LightWrite w;
        w.setBri = true; w.briPercent = 0.0;
        const std::string b = lightBody(w);
        EXPECT(has(b, "\"brightness\":1.00"));
        EXPECT(!has(b, "\"brightness\":0"));

        LightWrite over;
        over.setBri = true; over.briPercent = 250.0;
        EXPECT(has(lightBody(over), "\"brightness\":100.00"));
    }

    {
        // xy wins over mirek: a lamp is in colour mode or white mode, and
        // sending both would let the bridge choose.
        LightWrite w;
        w.setXy = true;    w.xy = Xy{ 0.5, 0.4 };
        w.setMirek = true; w.mirek = 300;
        const std::string b = lightBody(w);
        EXPECT(has(b, "\"color\":{\"xy\":{\"x\":0.5000,\"y\":0.4000}}"));
        EXPECT(!has(b, "color_temperature"));

        // …and mirek is written when xy is absent, clamped into 153..500.
        LightWrite ct;
        ct.setMirek = true; ct.mirek = 9000;
        EXPECT(has(lightBody(ct), "\"color_temperature\":{\"mirek\":500}"));
        LightWrite ct2;
        ct2.setMirek = true; ct2.mirek = 1;
        EXPECT(has(lightBody(ct2), "\"color_temperature\":{\"mirek\":153}"));
    }

    {
        // dynamics only appears when a duration was asked for.
        LightWrite quiet;
        quiet.setBri = true; quiet.briPercent = 50.0;
        EXPECT(!has(lightBody(quiet), "dynamics"));

        LightWrite timed = quiet;
        timed.durationMs = 100;
        EXPECT(has(lightBody(timed), "\"dynamics\":{\"duration\":100}"));

        // Zero is a legal duration ("snap"), not the same as "unset".
        LightWrite snap = quiet;
        snap.durationMs = 0;
        EXPECT(has(lightBody(snap), "\"dynamics\":{\"duration\":0}"));
    }

    {
        const std::string plain = sceneRecallBody(false, 400);
        EXPECT(has(plain, "\"action\":\"active\""));
        EXPECT(has(plain, "\"duration\":400"));
        EXPECT(!has(plain, "dimming"));

        const std::string dyn = sceneRecallBody(true, -1);
        EXPECT(has(dyn, "\"action\":\"dynamic_palette\""));
        EXPECT(!has(dyn, "duration"));

        EXPECT(has(sceneRecallBody(false, 0, 60.0), "\"brightness\":60.00"));
    }

    // ======================= HueClient: parsers =============================

    {
        const std::string json =
            "[{\"id\":\"ecb5fafffe1c9a30\",\"internalipaddress\":\"192.168.1.20\","
            "\"port\":443}]";
        const auto found = parseDiscovery(json);
        EXPECT(found.size() == 1);
        EXPECT(found[0].id == "ecb5fafffe1c9a30");
        EXPECT(found[0].ip == "192.168.1.20");

        EXPECT(parseDiscovery("[]").empty());
        EXPECT(parseDiscovery("not json").empty());
    }

    {
        // Error 101 is the pairing loop's normal state, not a failure: it means
        // the user has not walked over to the bridge yet.
        const auto waiting = parsePairResponse(
            "[{\"error\":{\"type\":101,\"address\":\"\","
            "\"description\":\"link button not pressed\"}}]");
        EXPECT(!waiting.ok);
        EXPECT(waiting.waiting);
        EXPECT(waiting.error.empty());

        const auto ok = parsePairResponse(
            "[{\"success\":{\"username\":\"APPKEY123\","
            "\"clientkey\":\"CLIENTKEY456\"}}]");
        EXPECT(ok.ok);
        EXPECT(!ok.waiting);
        EXPECT(ok.appKey == "APPKEY123");
        EXPECT(ok.clientKey == "CLIENTKEY456");

        const auto bad = parsePairResponse(
            "[{\"error\":{\"type\":7,\"description\":\"invalid value\"}}]");
        EXPECT(!bad.ok && !bad.waiting);
        EXPECT(bad.error == "invalid value");

        const auto junk = parsePairResponse("{}");
        EXPECT(!junk.ok && !junk.waiting && !junk.error.empty());
    }

    {
        const std::string json =
            "{\"errors\":[],\"data\":["
            "{\"id\":\"l-1\",\"type\":\"light\","
            "\"owner\":{\"rid\":\"dev-1\",\"rtype\":\"device\"},"
            "\"metadata\":{\"name\":\"Spot links\",\"archetype\":\"spot_bulb\"},"
            "\"on\":{\"on\":true},"
            "\"dimming\":{\"brightness\":42.5,\"min_dim_level\":0.2},"
            "\"color_temperature\":{\"mirek\":null,\"mirek_valid\":false},"
            "\"color\":{\"xy\":{\"x\":0.4,\"y\":0.35},"
            "\"gamut\":{\"red\":{\"x\":0.6915,\"y\":0.3083},"
            "\"green\":{\"x\":0.17,\"y\":0.7},"
            "\"blue\":{\"x\":0.1532,\"y\":0.0475}},\"gamut_type\":\"C\"}},"
            "{\"id\":\"l-2\",\"type\":\"light\","
            "\"metadata\":{\"name\":\"Flur\"},"
            "\"on\":{\"on\":false},"
            "\"color_temperature\":{\"mirek\":366,\"mirek_valid\":true}}"
            "]}";
        const auto lights = parseLights(json);
        EXPECT(lights.size() == 2);

        EXPECT(lights[0].id == "l-1");
        EXPECT(lights[0].name == "Spot links");
        EXPECT(lights[0].ownerRid == "dev-1");
        EXPECT(lights[0].on);
        EXPECT(lights[0].dimmable);
        EXPECT(near(lights[0].briPercent, 42.5, 1e-9));
        EXPECT(lights[0].hasXy);
        EXPECT(near(lights[0].xy.x, 0.4, 1e-9));
        EXPECT(lights[0].gamut.valid);
        EXPECT(near(lights[0].gamut.red.x, 0.6915, 1e-9));
        // mirek_valid false means the lamp is in colour mode; the number that
        // comes with it is stale and must not be believed.
        EXPECT(!lights[0].hasMirek);

        // A white-only lamp: no colour block at all, so no gamut and no xy.
        EXPECT(lights[1].name == "Flur");
        EXPECT(!lights[1].on);
        EXPECT(!lights[1].dimmable);
        EXPECT(!lights[1].hasXy);
        EXPECT(!lights[1].gamut.valid);
        EXPECT(lights[1].hasMirek && lights[1].mirek == 366);

        EXPECT(parseLights("{\"data\":[]}").empty());
        EXPECT(parseLights("garbage").empty());
    }

    {
        // ⇨ The writable id comes out of `services`, never off the room itself.
        const std::string json =
            "{\"errors\":[],\"data\":["
            "{\"id\":\"room-1\",\"type\":\"room\","
            "\"metadata\":{\"name\":\"Studio\"},"
            "\"children\":[{\"rid\":\"dev-1\",\"rtype\":\"device\"}],"
            "\"services\":[{\"rid\":\"gl-9\",\"rtype\":\"grouped_light\"},"
            "{\"rid\":\"other\",\"rtype\":\"scene\"}]},"
            "{\"id\":\"room-2\",\"type\":\"room\","
            "\"metadata\":{\"name\":\"Leer\"},\"services\":[]}"
            "]}";
        const auto rooms = parseGroups(json, /*zones=*/false);
        EXPECT(rooms.size() == 2);
        EXPECT(rooms[0].name == "Studio");
        EXPECT(rooms[0].groupedLightId == "gl-9");
        EXPECT(!rooms[0].isZone);
        // A room with no grouped_light service cannot be dimmed, and the empty
        // id is how the settings list knows not to offer it.
        EXPECT(rooms[1].groupedLightId.empty());

        const auto zones = parseGroups(json, /*zones=*/true);
        EXPECT(zones.size() == 2 && zones[0].isZone);
    }

    {
        const std::string json =
            "{\"errors\":[],\"data\":["
            "{\"id\":\"sc-1\",\"type\":\"scene\","
            "\"metadata\":{\"name\":\"Tracking\"},"
            "\"group\":{\"rid\":\"room-1\",\"rtype\":\"room\"},"
            "\"status\":{\"active\":\"static\"}},"
            "{\"id\":\"sc-2\",\"metadata\":{\"name\":\"Relax\"},"
            "\"group\":{\"rid\":\"room-1\",\"rtype\":\"room\"},"
            "\"status\":{\"active\":\"inactive\"}},"
            "{\"id\":\"sc-3\",\"metadata\":{\"name\":\"Party\"},"
            "\"status\":{\"active\":\"dynamic_palette\"}}"
            "]}";
        const auto scenes = parseScenes(json);
        EXPECT(scenes.size() == 3);
        EXPECT(scenes[0].name == "Tracking");
        EXPECT(scenes[0].groupRid == "room-1");
        EXPECT(scenes[0].active);
        EXPECT(!scenes[1].active);
        // Anything that is not "inactive" counts as showing, dynamic included.
        EXPECT(scenes[2].active);
    }

    {
        const std::string json =
            "{\"errors\":[],\"data\":[{\"id\":\"br-1\",\"type\":\"bridge\","
            "\"bridge_id\":\"ecb5fafffe1c9a30\","
            "\"time_zone\":{\"time_zone\":\"Europe/Zurich\"}}]}";
        EXPECT(parseBridgeId(json) == "ecb5fafffe1c9a30");
        // An empty answer must read as "not our bridge" at the call site — this
        // is the only identity check left with the certificate unverified.
        EXPECT(parseBridgeId("{\"data\":[]}").empty());
        EXPECT(parseBridgeId("nonsense").empty());
    }

    {
        std::string msg;
        EXPECT(!hasApiError("{\"errors\":[],\"data\":[]}", &msg));
        EXPECT(hasApiError(
            "{\"errors\":[{\"description\":\"device (light) has communication "
            "issues\"}],\"data\":[]}", &msg));
        EXPECT(msg.find("communication") != std::string::npos);
    }

    std::printf("test_hue: all good\n");
    return 0;
}
