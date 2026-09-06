#include "Sha256.h"

#include <cstring>
#include <vector>

namespace reasixty {
namespace {

constexpr uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void block(const unsigned char* p, uint32_t h[8])
{
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16)
             | (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t S1  = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
        const uint32_t ch  = (e & f) ^ (~e & g);
        const uint32_t t1  = hh + S1 + ch + kK[i] + w[i];
        const uint32_t S0  = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2  = S0 + maj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

constexpr char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

void sha256(const void* data, size_t len, unsigned char out[32])
{
    uint32_t h[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    const unsigned char* p = static_cast<const unsigned char*>(data);

    size_t off = 0;
    for (; off + 64 <= len; off += 64) block(p + off, h);

    // Tail: the remainder, 0x80, zeroes, then the length in BITS, big-endian.
    // One or two blocks, depending on whether the length still fits.
    unsigned char tail[128] = {0};
    const size_t rem = len - off;
    std::memcpy(tail, p + off, rem);
    tail[rem] = 0x80;
    const size_t tailLen = (rem >= 56) ? 128 : 64;
    const uint64_t bits = uint64_t(len) * 8u;
    for (int i = 0; i < 8; ++i)
        tail[tailLen - 1 - i] = static_cast<unsigned char>((bits >> (8 * i)) & 0xFF);
    block(tail, h);
    if (tailLen == 128) block(tail + 64, h);

    for (int i = 0; i < 8; ++i) {
        out[i * 4 + 0] = static_cast<unsigned char>((h[i] >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<unsigned char>((h[i] >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<unsigned char>((h[i] >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<unsigned char>(h[i] & 0xFF);
    }
}

std::string base64Encode(const void* data, size_t len)
{
    const unsigned char* p = static_cast<const unsigned char*>(data);
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        const uint32_t v = (uint32_t(p[i]) << 16) | (uint32_t(p[i + 1]) << 8) | p[i + 2];
        out += kB64[(v >> 18) & 0x3F];
        out += kB64[(v >> 12) & 0x3F];
        out += kB64[(v >> 6) & 0x3F];
        out += kB64[v & 0x3F];
    }
    if (i < len) {
        const size_t rem = len - i;                       // 1 or 2
        uint32_t v = uint32_t(p[i]) << 16;
        if (rem == 2) v |= uint32_t(p[i + 1]) << 8;
        out += kB64[(v >> 18) & 0x3F];
        out += kB64[(v >> 12) & 0x3F];
        out += (rem == 2) ? kB64[(v >> 6) & 0x3F] : '=';
        out += '=';
    }
    return out;
}

std::string sha256Base64(const std::string& s)
{
    unsigned char d[32];
    sha256(s.data(), s.size(), d);
    return base64Encode(d, sizeof(d));
}

}  // namespace reasixty
