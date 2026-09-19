#include "RmeOsc.h"

#include <cstring>

namespace reasixty::rme {
namespace {

std::size_t pad4(std::size_t n) { return (n + 3u) & ~std::size_t(3); }

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void putPadded(std::vector<std::uint8_t>& out, const std::string& s)
{
    out.insert(out.end(), s.begin(), s.end());
    const std::size_t total = pad4(s.size() + 1);
    for (std::size_t i = s.size(); i < total; ++i) out.push_back(0);
}

bool readU32(std::span<const std::uint8_t> b, std::size_t& i, std::uint32_t& out)
{
    if (i + 4 > b.size()) return false;
    out = (static_cast<std::uint32_t>(b[i]) << 24)
        | (static_cast<std::uint32_t>(b[i + 1]) << 16)
        | (static_cast<std::uint32_t>(b[i + 2]) << 8)
        | static_cast<std::uint32_t>(b[i + 3]);
    i += 4;
    return true;
}

// OSC strings are NUL-terminated and padded to a 4-byte boundary. A string with
// no terminator inside the packet is a malformed packet, not a string that runs
// to the end.
bool readString(std::span<const std::uint8_t> b, std::size_t& i, std::string& out)
{
    const std::size_t start = i;
    while (i < b.size() && b[i] != 0) ++i;
    if (i >= b.size()) return false;
    out.assign(reinterpret_cast<const char*>(b.data() + start), i - start);
    i = start + pad4(i - start + 1);
    return i <= b.size();
}

bool decodeMessage(std::span<const std::uint8_t> b, Message& out)
{
    std::size_t i = 0;
    if (!readString(b, i, out.address)) return false;
    if (out.address.empty() || out.address[0] != '/') return false;
    out.args.clear();
    if (i >= b.size()) return true;            // address only, no type tags
    std::string tags;
    if (!readString(b, i, tags)) return false;
    if (tags.empty() || tags[0] != ',') return false;
    for (std::size_t t = 1; t < tags.size(); ++t) {
        switch (tags[t]) {
            case 'f': {
                std::uint32_t raw = 0;
                if (!readU32(b, i, raw)) return false;
                float v = 0.0f;
                std::memcpy(&v, &raw, sizeof(v));
                out.args.push_back(Arg::fromFloat(v));
                break;
            }
            case 'i': {
                std::uint32_t raw = 0;
                if (!readU32(b, i, raw)) return false;
                out.args.push_back(Arg::fromInt(static_cast<std::int32_t>(raw)));
                break;
            }
            case 's': {
                std::string v;
                if (!readString(b, i, v)) return false;
                out.args.push_back(Arg::fromString(std::move(v)));
                break;
            }
            case 'T': out.args.push_back(Arg::fromFloat(1.0f)); break;
            case 'F': out.args.push_back(Arg::fromFloat(0.0f)); break;
            case 'b': {                         // skipped, but its length must be
                std::uint32_t n = 0;            // consumed or everything after
                if (!readU32(b, i, n)) return false;   // it decodes as garbage
                i += pad4(n);
                if (i > b.size()) return false;
                break;
            }
            default: return false;              // an unknown tag makes the rest
        }                                       // unreadable, so stop honestly
    }
    return true;
}

}  // namespace

std::vector<std::uint8_t> encode(const std::string& address,
                                 const std::vector<Arg>& args)
{
    std::vector<std::uint8_t> out;
    putPadded(out, address);
    std::string tags = ",";
    for (const Arg& a : args) {
        switch (a.type) {
            case Arg::Type::Float:  tags += 'f'; break;
            case Arg::Type::Int:    tags += 'i'; break;
            case Arg::Type::String: tags += 's'; break;
        }
    }
    putPadded(out, tags);
    for (const Arg& a : args) {
        switch (a.type) {
            case Arg::Type::Float: {
                std::uint32_t raw = 0;
                std::memcpy(&raw, &a.f, sizeof(raw));
                putU32(out, raw);
                break;
            }
            case Arg::Type::Int:
                putU32(out, static_cast<std::uint32_t>(a.i));
                break;
            case Arg::Type::String:
                putPadded(out, a.s);
                break;
        }
    }
    return out;
}

std::vector<std::uint8_t> encodeFloat(const std::string& address, float value)
{
    return encode(address, { Arg::fromFloat(value) });
}

std::size_t forEachMessage(std::span<const std::uint8_t> packet,
                           const std::function<void(const Message&)>& cb)
{
    if (packet.size() >= 8 && std::memcmp(packet.data(), "#bundle", 8) == 0) {
        // 8 bytes "#bundle\0" + 8 bytes time tag, then size-prefixed elements.
        std::size_t i = 16;
        std::size_t n = 0;
        while (i + 4 <= packet.size()) {
            std::uint32_t len = 0;
            if (!readU32(packet, i, len)) break;
            if (len == 0 || i + len > packet.size()) break;
            n += forEachMessage(packet.subspan(i, len), cb);
            i += len;
        }
        return n;
    }
    Message m;
    if (!decodeMessage(packet, m)) return 0;
    cb(m);
    return 1;
}

}  // namespace reasixty::rme
