//
// Unit tests for the pure pieces of OBS Mode: SHA-256, base64, and the
// obs-websocket message builders and parsers. No socket is opened here.
//
// Build: part of the reaper_uf8 CMake project (test_obs target).
//
// The JSON below is in the shape the protocol document specifies: the {"op":n,
// "d":{…}} envelope, authentication only present when the server wants a
// password, and requestStatus carrying result plus a comment when it failed.
//

#include "ObsProto.h"
#include "Sha256.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define EXPECT(cond) do {                                              \
    if (!(cond)) {                                                     \
        std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__,   \
                     #cond);                                           \
        std::exit(1);                                                  \
    }                                                                  \
} while(0)

static std::string hex(const unsigned char* d, int n)
{
    std::string s;
    char b[3];
    for (int i = 0; i < n; ++i) { std::snprintf(b, sizeof(b), "%02x", d[i]); s += b; }
    return s;
}

static bool has(const std::string& hay, const char* needle)
{
    return hay.find(needle) != std::string::npos;
}

int main()
{
    using namespace reasixty;
    using namespace reasixty::obs;

    // ======================= SHA-256, FIPS 180-4 vectors ====================
    {
        unsigned char d[32];
        sha256("", 0, d);
        EXPECT(hex(d, 32) ==
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        sha256("abc", 3, d);
        EXPECT(hex(d, 32) ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        const char* s2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        sha256(s2, std::strlen(s2), d);
        EXPECT(hex(d, 32) ==
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
        // Crosses the two-block tail, which is the case a hand-written padding
        // gets wrong.
        const std::string a(1000000, 'a');
        sha256(a.data(), a.size(), d);
        EXPECT(hex(d, 32) ==
               "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }

    // ======================= base64, RFC 4648 vectors =======================
    {
        EXPECT(base64Encode("", 0) == "");
        EXPECT(base64Encode("f", 1) == "Zg==");
        EXPECT(base64Encode("fo", 2) == "Zm8=");
        EXPECT(base64Encode("foo", 3) == "Zm9v");
        EXPECT(base64Encode("foob", 4) == "Zm9vYg==");
        EXPECT(base64Encode("fooba", 5) == "Zm9vYmE=");
        EXPECT(base64Encode("foobar", 6) == "Zm9vYmFy");
        EXPECT(sha256Base64("") ==
               "47DEQpj8HBSa+/TImW+5JCeuQeRkm5NMpJWZG3hSuFU=");
    }

    // ======================= the login string ===============================
    {
        // base64(sha256(base64(sha256(password + salt)) + challenge)), built the
        // long way here so the test would catch the two hashes being swapped.
        const std::string pw = "supersecretpassword";
        const std::string salt = "82KI2Ge5VUE=";
        const std::string chal = "+IxH4CnCiqpX1w==";
        const std::string secret = sha256Base64(pw + salt);
        EXPECT(authToken(pw, salt, chal) == sha256Base64(secret + chal));
        EXPECT(!authToken(pw, salt, chal).empty());
    }

    // ======================= outbound shapes ================================
    {
        const std::string id = identify("AUTH", kEventSubscriptions);
        EXPECT(has(id, "\"op\":1"));
        EXPECT(has(id, "\"rpcVersion\":1"));
        EXPECT(has(id, "\"authentication\":\"AUTH\""));
        EXPECT(has(id, "\"eventSubscriptions\":68"));      // Scenes | Outputs

        // No password on the other side: the field must be absent, not empty.
        EXPECT(!has(identify("", kEventSubscriptions), "authentication"));

        const std::string r = request("ToggleRecord", "r1");
        EXPECT(has(r, "\"op\":6"));
        EXPECT(has(r, "\"requestType\":\"ToggleRecord\""));
        EXPECT(has(r, "\"requestId\":\"r1\""));
        EXPECT(!has(r, "requestData"));

        const std::string sc = request("SetCurrentProgramScene", "s1",
                                       sceneSwitchData("Wide \"A\""));
        EXPECT(has(sc, "\"sceneName\":\"Wide \\\"A\\\"\""));   // quotes escaped

        EXPECT(chapterData("").empty());                        // OBS names it
        EXPECT(has(chapterData("Take 3"), "\"chapterName\":\"Take 3\""));
        EXPECT(jsonEscape("a\nb") == "a\\nb");
    }

    // ======================= Hello ==========================================
    {
        Message m;
        EXPECT(parse("{\"op\":0,\"d\":{\"obsWebSocketVersion\":\"5.5.4\","
                     "\"rpcVersion\":1,\"obsStudioVersion\":\"32.2.2\","
                     "\"authentication\":{\"challenge\":\"CHAL\","
                     "\"salt\":\"SALT\"}}}", m));
        EXPECT(m.kind == MsgKind::Hello);
        EXPECT(m.authRequired);
        EXPECT(m.salt == "SALT");
        EXPECT(m.challenge == "CHAL");
        EXPECT(m.rpcVersion == 1);
        EXPECT(m.obsVersion == "32.2.2");

        // Server without a password: same message, no authentication object.
        EXPECT(parse("{\"op\":0,\"d\":{\"rpcVersion\":1}}", m));
        EXPECT(m.kind == MsgKind::Hello);
        EXPECT(!m.authRequired);
    }

    // ======================= Identified =====================================
    {
        Message m;
        EXPECT(parse("{\"op\":2,\"d\":{\"negotiatedRpcVersion\":1}}", m));
        EXPECT(m.kind == MsgKind::Identified);
        EXPECT(m.rpcVersion == 1);
    }

    // ======================= record state ===================================
    {
        Message m;
        EXPECT(parse("{\"op\":5,\"d\":{\"eventType\":\"RecordStateChanged\","
                     "\"eventIntent\":64,\"eventData\":{\"outputActive\":true,"
                     "\"outputState\":\"OBS_WEBSOCKET_OUTPUT_STARTED\"}}}", m));
        EXPECT(m.kind == MsgKind::Event);
        EXPECT(m.hasRecord);
        EXPECT(m.recordActive);
        EXPECT(!m.recordPaused);
        EXPECT(m.recordState == "OBS_WEBSOCKET_OUTPUT_STARTED");

        // Stopped, and with the path OBS wrote — the flag is what the LED reads.
        EXPECT(parse("{\"op\":5,\"d\":{\"eventType\":\"RecordStateChanged\","
                     "\"eventData\":{\"outputActive\":false,\"outputPath\":"
                     "\"/tmp/x.mov\",\"outputState\":"
                     "\"OBS_WEBSOCKET_OUTPUT_STOPPED\"}}}", m));
        EXPECT(m.hasRecord);
        EXPECT(!m.recordActive);

        // GetRecordStatus carries the same three facts on the response side.
        EXPECT(parse("{\"op\":7,\"d\":{\"requestType\":\"GetRecordStatus\","
                     "\"requestId\":\"g1\",\"requestStatus\":{\"result\":true,"
                     "\"code\":100},\"responseData\":{\"outputActive\":true,"
                     "\"outputPaused\":true,\"outputTimecode\":\"00:00:12.345\","
                     "\"outputDuration\":12345}}}", m));
        EXPECT(m.kind == MsgKind::Response);
        EXPECT(m.ok);
        EXPECT(m.hasRecord);
        EXPECT(m.recordActive);
        EXPECT(m.recordPaused);
        EXPECT(m.recordTimecode == "00:00:12.345");
    }

    // ======================= scenes =========================================
    {
        Message m;
        EXPECT(parse("{\"op\":7,\"d\":{\"requestType\":\"GetSceneList\","
                     "\"requestId\":\"l0\",\"requestStatus\":{\"result\":true},"
                     "\"responseData\":{\"currentProgramSceneName\":\"Wide\","
                     "\"scenes\":[{\"sceneName\":\"Wide\",\"sceneIndex\":1},"
                     "{\"sceneName\":\"Close\",\"sceneIndex\":0}]}}}", m));
        EXPECT(m.hasScenes);
        EXPECT(m.scenes.size() == 2);
        EXPECT(m.currentScene == "Wide");
        // Taken in the order OBS sends, which is what sceneAt() hands the keys.
        EXPECT(m.scenes[0] == "Wide");
        EXPECT(m.scenes[1] == "Close");

        EXPECT(parse("{\"op\":5,\"d\":{\"eventType\":"
                     "\"CurrentProgramSceneChanged\",\"eventData\":"
                     "{\"sceneName\":\"Close\"}}}", m));
        EXPECT(m.hasScene);
        EXPECT(m.sceneName == "Close");
    }

    // ======================= a refused request ==============================
    {
        Message m;
        EXPECT(parse("{\"op\":7,\"d\":{\"requestType\":\"CreateRecordChapter\","
                     "\"requestId\":\"c1\",\"requestStatus\":{\"result\":false,"
                     "\"code\":501,\"comment\":\"Output not active\"}}}", m));
        EXPECT(m.kind == MsgKind::Response);
        EXPECT(!m.ok);
        EXPECT(m.comment == "Output not active");
        EXPECT(!m.hasRecord);
    }

    // ======================= rubbish in ======================================
    {
        Message m;
        EXPECT(!parse("", m));
        EXPECT(!parse("not json", m));
        EXPECT(!parse("{\"nope\":1}", m));           // no op field
        EXPECT(parse("{\"op\":99,\"d\":{}}", m));    // unknown op is not an error
        EXPECT(m.kind == MsgKind::Unknown);
    }

    std::printf("test_obs: all good\n");
    return 0;
}
