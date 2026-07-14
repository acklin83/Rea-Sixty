#include "SslMeterProtocol.h"

#include <cstring>

namespace sslmeter {
namespace {

// Read a base-128 varint. Advances `i`; returns false on truncation.
bool readVarint(const uint8_t* b, size_t len, size_t& i, uint64_t& out)
{
    uint64_t r = 0;
    int shift = 0;
    while (i < len) {
        const uint8_t x = b[i++];
        r |= uint64_t(x & 0x7f) << shift;
        if (!(x & 0x80)) { out = r; return true; }
        shift += 7;
        if (shift >= 64) return false;
    }
    return false;
}

float readF32LE(const uint8_t* b)
{
    float f;
    std::memcpy(&f, b, 4);   // every target is little-endian
    return f;
}

uint32_t readU32LE(const uint8_t* b)
{
    uint32_t v;
    std::memcpy(&v, b, 4);
    return v;
}

} // namespace

bool parseMeterMessage(const uint8_t* body, size_t len, Update& out)
{
    if (len <= kSslHeaderLen) return false;
    const uint8_t* b = body + kSslHeaderLen;   // protobuf starts after the header
    const size_t   L = len - kSslHeaderLen;

    Update u;
    size_t i = 0;
    while (i < L) {
        uint64_t tag;
        if (!readVarint(b, L, i, tag)) break;
        const uint32_t field = uint32_t(tag >> 3);
        const uint32_t wire  = uint32_t(tag & 7);
        if (field == 0) break;

        switch (wire) {
            case 0: {                                   // varint
                uint64_t v;
                if (!readVarint(b, L, i, v)) return u.valid() ? (out = std::move(u), true) : false;
                if (field == 1)      u.pluginType = int(v);
                else if (field == 2) u.dataType   = int(v);
                break;
            }
            case 5: {                                   // fixed32 (float)
                if (i + 4 > L) return u.valid() ? (out = std::move(u), true) : false;
                const float f = readF32LE(b + i);
                i += 4;
                if (field == 3)      u.current.push_back(f);
                else if (field == 4) u.peak.push_back(f);
                break;
            }
            case 1: {                                   // fixed64
                if (i + 8 > L) return u.valid() ? (out = std::move(u), true) : false;
                i += 8;
                break;
            }
            case 2: {                                   // length-delimited
                uint64_t n;
                if (!readVarint(b, L, i, n)) return u.valid() ? (out = std::move(u), true) : false;
                if (i + n > L) return u.valid() ? (out = std::move(u), true) : false;
                i += size_t(n);
                break;
            }
            default:                                    // 3,4 (groups) / 6,7 — unexpected
                return u.valid() ? (out = std::move(u), true) : false;
        }
    }

    if (!u.valid()) return false;
    out = std::move(u);
    return true;
}

int parseDatagram(const uint8_t* data, size_t len, std::vector<Update>& out)
{
    int count = 0;
    size_t p = 0;
    while (p + kMagicLen + kFrameLenSize <= len) {
        if (std::memcmp(data + p, kMagic, kMagicLen) != 0) break;
        const uint32_t frameLen = readU32LE(data + p + kMagicLen);
        const size_t   bodyOff  = p + kMagicLen + kFrameLenSize;
        if (bodyOff + frameLen > len) break;            // truncated frame
        Update u;
        if (parseMeterMessage(data + bodyOff, frameLen, u)) {
            out.push_back(std::move(u));
            ++count;
        }
        p = bodyOff + frameLen;
    }
    return count;
}

} // namespace sslmeter
