#pragma once
//
// SslMeterProtocol — decode SSL 360° plugin meter data (PluginMeterDataMessage).
//
// The SSL Meter / Meter Pro plugin publishes its live meter values as plain,
// unencrypted Google-Protobuf over UDP to 360°Core (reverse-engineered
// 2026-07-10; schema in analysis/ssl360-protobuf/, wire notes in
// captures/cap87_ssl360_meter_protobuf.md). Once we impersonate Core, the plugin
// streams these to us and we drive the UF1 Meter/Analyzer view — no DSP, no
// pixel-reverse. Same path also carries CS/BC gain-reduction + the EQ curve.
//
// This module is the pure wire-decode half (no sockets, no REAPER API) so it can
// be unit-tested against captured frames. All multi-byte fields are little-endian
// (every target platform — mac/win/linux — is LE).
//
// Frame on the wire (one UDP datagram may carry several):
//   [ 'ef bc 51 00' ][ uint32 frame_len ][ 28-byte SSL/Lgx header ][ protobuf ]
// protobuf = PluginMeterDataMessage:
//   f1 PluginType (varint), f2 DataType (varint),
//   f3 CurrentMeterValues (repeated fixed32), f4 PeakValues (repeated fixed32),
//   f5/f6 Overload* (repeated bool), f7 MaxValueCount, ...
// Silence sentinel float = 0xffc00000 (-NaN) — decoded as -inf; callers floor it.

#include <cstdint>
#include <cstddef>
#include <vector>

namespace sslmeter {

// MeterPluginDataType (AssignerArgsTypes.proto). Values are the on-wire f2.
enum class DataType : int {
    VuPpm = 0, TextVuPpm = 1, BarPeak = 2, BarRms = 3, TextPeak = 4, TextRms = 5,
    Correlation = 6, StereoBalance = 7, Rta31Band = 8, TextRta = 9, Lissajous = 10,
    LoudMomentary = 11, LoudShortTerm = 12, LoudRangeLow = 13, LoudRangeHigh = 14,
    LoudReadout1 = 15, LoudReadout2 = 16, LoudReadout3 = 17, LoudReadout4 = 18,
    LoudReadout5 = 19, LoudReadout6 = 20, LoudReadout7 = 21, LoudReadout8 = 22,
    LoudReadout9 = 23, LoudReadout10 = 24, LoudCompleteHistory = 25,
    LoudScrollableHistory = 26, LoudHistogram = 27,
    Count = 28,
};

// PluginType (AssignerArgsTypes.proto). f1 of the message (often absent).
enum class PluginType : int {
    Unknown = 0, NativeChannelStrip = 1, NativeBusComp = 2, K4_B = 3, Meter = 4,
    K4_E = 5, GenericChannelStrip = 6, GenericBusComp = 7, MeterPro = 8, K4_G = 9,
};

constexpr uint8_t  kMagic[4]     = { 0xef, 0xbc, 0x51, 0x00 };
constexpr size_t   kMagicLen     = 4;
constexpr size_t   kFrameLenSize = 4;   // uint32 after the magic
constexpr size_t   kSslHeaderLen = 28;  // Lgx header before the protobuf

struct Update {
    int                pluginType = -1;  // PluginType, -1 if the field was absent
    int                dataType   = -1;  // DataType (f2)
    std::vector<float> current;          // CurrentMeterValues (f3)
    std::vector<float> peak;             // PeakValues (f4)

    bool valid() const { return dataType >= 0 && dataType < int(DataType::Count); }
};

// Decode the frame body (the bytes AFTER the 8-byte magic+len prefix) as a
// PluginMeterDataMessage. Returns true and fills `out` if f2 is a valid DataType.
bool parseMeterMessage(const uint8_t* body, size_t len, Update& out);

// Walk every SSL efbc frame in one UDP datagram payload, appending each decoded
// meter message to `out`. Returns the number appended. Malformed / non-meter
// frames are skipped; parsing stops at the first byte that isn't a frame magic.
int parseDatagram(const uint8_t* data, size_t len, std::vector<Update>& out);

} // namespace sslmeter
