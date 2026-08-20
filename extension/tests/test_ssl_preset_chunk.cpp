//
// Rewriting an SSL plug-in's VST3 chunk so the instance SHOWS a loaded preset
// (LastLoadedPreset) instead of "that preset, modified".
//
// What is pinned here is the part that can silently wreck a plug-in instance:
// the edit changes the XML's LENGTH, and the container counts bytes in three
// places (the "VC2!" length in front of the XML, plus two blob lengths in the
// header that sit at a plug-in-dependent OFFSET and are therefore found by
// value). Get one of them wrong and the plug-in loads nothing, or garbage.
//
// The layout below is the one measured on 113 real SSL blocks out of Frank's
// template chains and test project (2026-08-20); the same rewrite was run
// against a real 837 KB Meter Pro chunk before this test existed.
//
// Build: part of the reaper_uf8 CMake project (test_ssl_preset_chunk target).
//

// patchSslChunkWithPreset is file-local, so the translation unit is pulled in
// whole rather than widening its linkage for a test. REAPERAPI_IMPLEMENT
// defines the API pointers in this TU — nothing here calls them, they only have
// to resolve at link time.
#define REAPERAPI_IMPLEMENT
#include "PluginChunkPatch.cpp"

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

namespace {

void put32(std::string& s, size_t o, uint32_t v) {
    s[o]     = (char)( v        & 0xFF);
    s[o + 1] = (char)((v >> 8)  & 0xFF);
    s[o + 2] = (char)((v >> 16) & 0xFF);
    s[o + 3] = (char)((v >> 24) & 0xFF);
}
uint32_t get32(const std::string& s, size_t o) {
    return  (uint32_t)(uint8_t)s[o]
         | ((uint32_t)(uint8_t)s[o + 1] << 8)
         | ((uint32_t)(uint8_t)s[o + 2] << 16)
         | ((uint32_t)(uint8_t)s[o + 3] << 24);
}

// A blob shaped like the real thing: 48 bytes of header, the two 16-apart blob
// lengths at 48 / 60, "VC2!" + XML length, the XML, and a trailing block the
// rewrite must not touch. `lenOff` moves the length pair, because on real
// plug-ins it sits at 48/60 or at 64/76 depending on the header shape.
std::string makeChunk(const std::string& xml, size_t lenOff = 48,
                      size_t suffixLen = 46)
{
    const size_t prefix = lenOff + 28;          // … lenA … lenB … "VC2!" + len
    std::string b(prefix, '\0');
    put32(b, 4, 0xFEED5EEEu);                   // REAPER's marker, byte for byte
    b.replace(prefix - 8, 4, "VC2!");
    put32(b, prefix - 4, (uint32_t)xml.size());
    b += xml;
    b += std::string(suffixLen, '\x7f');        // must survive verbatim
    put32(b, lenOff,      (uint32_t)(b.size() - 66));
    put32(b, lenOff + 12, (uint32_t)(b.size() - 82));
    return b;
}

const char* kState =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?> "
    "<SSL_PLUGIN_STATE PluginName=\"SSL Meter Pro\" Version=\"1.2.8\" "
    "StateASelected=\"1\">"
    "<A LastLoadedPreset=\"/x/Default Preset.xml\"><PROCESSOR_STATE>"
    "<PARAM id=\"DigitalMetersType\" value=\"0.0\"/>"
    "<PARAM id=\"AnalogueMetersRefLevel\" value=\"-18.0\"/>"
    "<PARAM_NON_AUTO id=\"360SelectedView\" value=\"0.0\"/>"
    "</PROCESSOR_STATE></A>"
    "<B><PROCESSOR_STATE>"
    "<PARAM id=\"DigitalMetersType\" value=\"9.9\"/>"
    "</PROCESSOR_STATE></B>"
    "<PARAM_NON_AUTO id=\"HostTrackIndex\" value=\"7\"/>"
    "</SSL_PLUGIN_STATE>";

const char* kPreset =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<SSL_PRESET PluginName=\"SSL Meter Pro\"><PROCESSOR_STATE>"
    "<PARAM id=\"DigitalMetersType\" value=\"4.0\"/>"
    "<PARAM id=\"AnalogueMetersRefLevel\" value=\"-20.0\"/>"
    "<PARAM id=\"NotOnThisVariant\" value=\"1.0\"/>"
    "<PARAM_NON_AUTO id=\"360SelectedView\" value=\"2.0\"/>"
    "</PROCESSOR_STATE></SSL_PRESET>";

const char* kPath = "/Library/Application Support/Solid State Logic/"
                    "PlugIns/Presets/MeterPro/K-20.xml";

// The three byte counts and the untouched tail, after any rewrite.
void checkInvariants(const std::string& b, size_t lenOff, size_t suffixLen) {
    const size_t xs = b.find("<?xml");
    const size_t xe = b.find("</SSL_PLUGIN_STATE>") + 19;
    EXPECT(xs != std::string::npos && xe > xs);
    EXPECT(b.compare(xs - 8, 4, "VC2!") == 0);
    EXPECT(get32(b, xs - 4) == (uint32_t)(xe - xs));
    EXPECT(get32(b, lenOff) == (uint32_t)(b.size() - 66));
    EXPECT(get32(b, lenOff + 12) == get32(b, lenOff) - 16);
    EXPECT(get32(b, 4) == 0xFEED5EEEu);
    EXPECT(b.substr(xe) == std::string(suffixLen, '\x7f'));
}

void testRewrite(size_t lenOff) {
    std::string b = makeChunk(kState, lenOff);
    const int n = uf8::patchSslChunkWithPreset(b, kPath, kPreset);
    EXPECT(n == 3);                     // the id the variant lacks is skipped
    checkInvariants(b, lenOff, 46);

    const std::string xml = b.substr(b.find("<?xml"));
    // Values landed in the ACTIVE slot …
    EXPECT(xml.find("<PARAM id=\"DigitalMetersType\" value=\"4.0\"/>")
           != std::string::npos);
    EXPECT(xml.find("value=\"-20.0\"") != std::string::npos);
    EXPECT(xml.find("id=\"360SelectedView\" value=\"2.0\"") != std::string::npos);
    // … and NOT in the inactive one, nor on the instance's own identity.
    EXPECT(xml.find("value=\"9.9\"") != std::string::npos);
    EXPECT(xml.find("id=\"HostTrackIndex\" value=\"7\"") != std::string::npos);
    // The preset is now what the plug-in reports as loaded.
    EXPECT(xml.find(std::string("LastLoadedPreset=\"") + kPath + "\"")
           != std::string::npos);
    EXPECT(xml.find("Default Preset.xml") == std::string::npos);
}

// StateASelected="0" means B is the live slot; the same rewrite must land there.
void testInactiveSlotUntouched() {
    std::string st = kState;
    st.replace(st.find("StateASelected=\"") + 16, 1, "0");
    std::string b = makeChunk(st);
    const int n = uf8::patchSslChunkWithPreset(b, kPath, kPreset);
    EXPECT(n == 1);                     // B only carries DigitalMetersType
    checkInvariants(b, 48, 46);
    const std::string xml = b.substr(b.find("<?xml"));
    const size_t a = xml.find("<A "), bb = xml.find("<B");
    EXPECT(a != std::string::npos && bb > a);
    EXPECT(xml.find("value=\"0.0\"", a) < bb);          // A kept its value
    EXPECT(xml.find("value=\"4.0\"", bb) != std::string::npos);
    // B has no LastLoadedPreset attribute yet — it must be inserted, not lost.
    EXPECT(xml.find(std::string("LastLoadedPreset=\"") + kPath + "\"", bb)
           != std::string::npos);
}

// Anything that does not match the measured shape must leave the blob ALONE,
// so the caller can fall back to setting host parameters.
void testRefusesUnknownShapes() {
    {   // the length in front of the XML disagrees
        std::string b = makeChunk(kState);
        put32(b, b.find("<?xml") - 4, 12345);
        const std::string before = b;
        EXPECT(uf8::patchSslChunkWithPreset(b, kPath, kPreset) == 0);
        EXPECT(b == before);
    }
    {   // no "VC2!" in front of the XML
        std::string b = makeChunk(kState);
        b.replace(b.find("<?xml") - 8, 4, "XXXX");
        const std::string before = b;
        EXPECT(uf8::patchSslChunkWithPreset(b, kPath, kPreset) == 0);
        EXPECT(b == before);
    }
    {   // the two blob lengths are not 16 apart
        std::string b = makeChunk(kState);
        put32(b, 60, get32(b, 48) - 20);
        const std::string before = b;
        EXPECT(uf8::patchSslChunkWithPreset(b, kPath, kPreset) == 0);
        EXPECT(b == before);
    }
    {   // a preset naming nothing this instance has
        std::string b = makeChunk(kState);
        const std::string before = b;
        EXPECT(uf8::patchSslChunkWithPreset(
                   b, kPath,
                   "<PROCESSOR_STATE><PARAM id=\"Nope\" value=\"1.0\"/>"
                   "</PROCESSOR_STATE>") == 0);
        EXPECT(b == before);
    }
    {   // not an SSL state at all
        std::string b = makeChunk("<?xml ?><OTHER_PLUGIN/>");
        const std::string before = b;
        EXPECT(uf8::patchSslChunkWithPreset(b, kPath, kPreset) == 0);
        EXPECT(b == before);
    }
}

}  // namespace

int main() {
    testRewrite(48);        // header shape with the int at offset 8 == 2
    testRewrite(64);        // … and == 4, where the length pair moves
    testInactiveSlotUntouched();
    testRefusesUnknownShapes();
    std::printf("test_ssl_preset_chunk: all passed\n");
    return 0;
}
