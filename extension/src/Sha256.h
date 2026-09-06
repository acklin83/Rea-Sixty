#pragma once
//
// SHA-256, because obs-websocket's login needs it and the tree has none.
//
// WDL ships SHA-1 (vendor/WDL/WDL/sha.h), which covers the WebSocket handshake's
// Sec-WebSocket-Accept and nothing else. obs-websocket v5 authenticates with
//
//     base64(sha256(base64(sha256(password + salt)) + challenge))
//
// so the digest itself has to exist here. FIPS 180-4, no dependencies, no
// allocation. Tested against the standard vectors in tests/test_obs.cpp.
//
// Not a general crypto layer and not meant to become one: this is a hash for a
// loopback handshake, not a security boundary.

#include <cstddef>
#include <cstdint>
#include <string>

namespace reasixty {

// Raw 32-byte digest of `len` bytes at `data`.
void sha256(const void* data, size_t len, unsigned char out[32]);

// The two shapes obs-websocket actually asks for.
std::string sha256Base64(const std::string& s);          // base64(sha256(s))
std::string base64Encode(const void* data, size_t len);  // plain base64

}  // namespace reasixty
