#pragma once
//
// RmeOsc — the slice of OSC 1.0 that TotalMix FX's Global OSC actually uses.
//
// Scope, deliberately small: float / int32 / string arguments, big-endian,
// 4-byte aligned, and BUNDLES, because every TotalMix-to-host packet arrives
// wrapped in one (measured 2026-09-19 against TotalMix 2.10 alpha 8 on OSC
// Remote 1). No address-pattern matching: TotalMix sends concrete paths and a
// string compare is the whole dispatcher.
//
// No liblo, no oscpack. This is ~150 lines of spec against a dependency that
// would be a hundred times that, and Frank's TotalReaper made the same call for
// the same reason.
//
// Pure: no sockets, no REAPER, no threads. That is what lets it be the part
// with a test (tests/test_rme_osc.cpp) while the socket half is exercised
// against the real mixer by tools/rme_osc_probe.
//

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace reasixty::rme {

struct Arg {
    enum class Type : std::uint8_t { Float, Int, String };
    Type        type = Type::Float;
    float       f    = 0.0f;
    std::int32_t i   = 0;
    std::string  s;

    static Arg fromFloat(float v)        { Arg a; a.type = Type::Float;  a.f = v; return a; }
    static Arg fromInt(std::int32_t v)   { Arg a; a.type = Type::Int;    a.i = v; return a; }
    static Arg fromString(std::string v) { Arg a; a.type = Type::String; a.s = std::move(v); return a; }

    // TotalMix answers "0.0 / 1.0" for flags but the table says values are also
    // accepted as int and as T/F, so a reader that only looks at `f` would drop
    // half of them. Everything numeric comes through here.
    double number() const
    {
        switch (type) {
            case Type::Float:  return static_cast<double>(f);
            case Type::Int:    return static_cast<double>(i);
            case Type::String: return 0.0;
        }
        return 0.0;
    }
};

// One decoded message. Address plus arguments, nothing else.
struct Message {
    std::string      address;
    std::vector<Arg> args;
};

// Encode a single message. `address` must start with '/'.
std::vector<std::uint8_t> encode(const std::string& address,
                                 const std::vector<Arg>& args);

// Convenience: the shape almost every command takes ("/controlroom/dim", 1.0f).
std::vector<std::uint8_t> encodeFloat(const std::string& address, float value);

// Walk a received datagram and hand every message inside it to `cb`, descending
// into bundles (and bundles inside bundles). Returns the number of messages
// dispatched; 0 means the packet was not something we understand, which is a
// state to report and not to crash on.
std::size_t forEachMessage(std::span<const std::uint8_t> packet,
                           const std::function<void(const Message&)>& cb);

}  // namespace reasixty::rme
