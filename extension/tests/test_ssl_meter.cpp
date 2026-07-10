//
// Unit tests for SslMeterProtocol — decode SSL 360° PluginMeterDataMessage.
// Test vectors are REAL frames carved from captures/cap87_ssl360_meter_protobuf
// (port 50881, a stereo SSL Meter Pro instance). See analysis/ssl360-protobuf/.
//

#include "SslMeterProtocol.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

#define EXPECT(cond) do {                                              \
    if (!(cond)) {                                                     \
        std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__,   \
                     #cond);                                           \
        std::exit(1);                                                  \
    }                                                                  \
} while(0)

static std::vector<uint8_t> unhex(const char* s)
{
    auto nyb = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<uint8_t> v;
    for (; s[0] && s[1]; s += 2) v.push_back(uint8_t(nyb(s[0]) << 4 | nyb(s[1])));
    return v;
}

// Real single-frame UDP datagrams from cap87 (port 50881, stereo Meter Pro).
static const char* kBarPeak =
    "efbc51003a0000001000000001000000449c28172e0000000300000079057a5f1c010000"
    "10021d7b48edc01dd20ad4c025212b8ac0253ed2a7c02800280030003000";
static const char* kBarRms =
    "efbc51003a0000001000000001000000449c28172e0000000300000079057a5f1c010000"
    "10031df782a9c11d36e6a8c1259a9901c3259a9901c32800280030003000";
static const char* kCorrelation =
    "efbc51002c0000001000000001000000449c281720000000030000007905"
    "7a5f1c01000010061d55f6ba3e250000000028003000";
static const char* kStereoBalance =
    "efbc51002c0000001000000001000000449c281720000000030000007905"
    "7a5f1c01000010071d4ac2543c250000000028003000";

static bool approx(float a, float b) { return std::fabs(a - b) < 0.01f; }

int main()
{
    using namespace sslmeter;

    // --- BarPeak: DataType 2, stereo (L,R) current + peak, values in dBFS. ---
    {
        auto d = unhex(kBarPeak);
        std::vector<Update> ups;
        EXPECT(parseDatagram(d.data(), d.size(), ups) == 1);
        const Update& u = ups[0];
        EXPECT(u.dataType == int(DataType::BarPeak));
        EXPECT(u.current.size() == 2);
        EXPECT(u.peak.size() == 2);
        // 0xc0ed487b, 0xc0d40ad2  ->  -7.415, -6.627 dBFS
        EXPECT(approx(u.current[0], -7.415f));
        EXPECT(approx(u.current[1], -6.627f));
        // peak 0xc08a2b21, 0xc0a7d23e  ->  -4.317, -5.245
        EXPECT(approx(u.peak[0], -4.317f));
        EXPECT(approx(u.peak[1], -5.245f));
    }

    // --- BarRms: DataType 3, stereo. R peak here is a silence-ish -129.6 floor. ---
    {
        auto d = unhex(kBarRms);
        std::vector<Update> ups;
        EXPECT(parseDatagram(d.data(), d.size(), ups) == 1);
        EXPECT(ups[0].dataType == int(DataType::BarRms));
        EXPECT(ups[0].current.size() == 2);
        EXPECT(ups[0].current[0] < 0.0f && ups[0].current[0] > -140.0f);
    }

    // --- Correlation: DataType 6, single value in -1..1. ---
    {
        auto d = unhex(kCorrelation);
        std::vector<Update> ups;
        EXPECT(parseDatagram(d.data(), d.size(), ups) == 1);
        EXPECT(ups[0].dataType == int(DataType::Correlation));
        EXPECT(ups[0].current.size() == 1);
        EXPECT(approx(ups[0].current[0], 0.3652f));   // 0x3ebaf655
    }

    // --- StereoBalance: DataType 7, single value near centre. ---
    {
        auto d = unhex(kStereoBalance);
        std::vector<Update> ups;
        EXPECT(parseDatagram(d.data(), d.size(), ups) == 1);
        EXPECT(ups[0].dataType == int(DataType::StereoBalance));
        EXPECT(ups[0].current.size() == 1);
        EXPECT(approx(ups[0].current[0], 0.01298f));  // 0x3c54c24a
    }

    // --- Multiple frames concatenated in one datagram all decode. ---
    {
        auto a = unhex(kBarPeak), b = unhex(kCorrelation), c = unhex(kStereoBalance);
        std::vector<uint8_t> all;
        all.insert(all.end(), a.begin(), a.end());
        all.insert(all.end(), b.begin(), b.end());
        all.insert(all.end(), c.begin(), c.end());
        std::vector<Update> ups;
        EXPECT(parseDatagram(all.data(), all.size(), ups) == 3);
        EXPECT(ups[0].dataType == int(DataType::BarPeak));
        EXPECT(ups[1].dataType == int(DataType::Correlation));
        EXPECT(ups[2].dataType == int(DataType::StereoBalance));
    }

    // --- Garbage / truncation is rejected, not crashed. ---
    {
        std::vector<Update> ups;
        const uint8_t junk[] = { 0x00, 0x11, 0x22, 0x33, 0x44 };
        EXPECT(parseDatagram(junk, sizeof(junk), ups) == 0);
        auto d = unhex(kBarPeak);
        EXPECT(parseDatagram(d.data(), 10, ups) == 0);   // truncated frame
    }

    std::printf("test_ssl_meter: all passed\n");
    return 0;
}
