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
    std::vector<uint8_t> overload;       // OverloadValues (f5) — the red LEDs
    std::vector<uint8_t> overloadHold;   // OverloadInfHoldValues (f6)

    // Chunking. A big array (the Lissajous) does NOT fit one message: the plugin
    // splits it and each part carries these. MEASURED — the Lissajous is 17113
    // floats total and arrives in pieces. THREE conventions seen so far, and only
    // dt=10 is ever chunked (every other type has f7=f8=f9 absent):
    //   cap87 src 59950: f7=17113, f8=1500 (ACTUAL size, 613 on the tail),
    //                    f9 = 0,1500,3000…16500  -> offset in FLOATS
    //   cap87 src 59625: f7 ABSENT, f8=5000 (NOMINAL, stays 5000 on the tail),
    //                    f9 = 0,1,2,3            -> offset is a chunk INDEX
    //   live 2026-07-15: f7 ABSENT, f8=5000, f9 = 0,1,2,3, sizes 5000,5000,5000,
    //                    2113  -> 17113, same as cap87's f7. Both plug-in
    //                    instances did this.
    // So f7 may be ABSENT: do not require it (that bug made the first probe run
    // dump chunks instead of images). Detect the tail by a short chunk instead.
    // Before any of this was understood we overwrote the slot with each chunk and
    // kept whichever arrived last — which is the whole reason the "array length"
    // looked like it alternated between 5000 and 2113 (= 17113 - 3*5000).
    int maxCount    = -1;  // MaxValueCount (f7) — total floats; OFTEN ABSENT
    int chunkSize   = -1;  // ChunkSize      (f8) — nominal OR actual, sender-dependent
    int chunkOffset = -1;  // ChunkOffset    (f9) — index OR float offset

    bool valid() const { return dataType >= 0 && dataType < int(DataType::Count); }

    // Only the chunked types carry f8 at all, so its presence IS the flag.
    bool chunked() const { return chunkSize > 0; }

    // True when this chunk closes the array. With f7 we simply know the total;
    // without it, the tail is the chunk shorter than the nominal chunk size.
    bool isTailChunk() const {
        if (maxCount > 0) return chunkStartIndex() + current.size() >= size_t(maxCount);
        return current.size() < size_t(chunkSize);
    }

    // Total floats, once known. 0 = not yet determined (f7 absent, tail not seen).
    size_t totalCount() const {
        if (maxCount > 0) return size_t(maxCount);
        if (current.size() < size_t(chunkSize)) return chunkStartIndex() + current.size();
        return 0;
    }

    // Where this chunk's floats start in the full array. The two observed senders
    // disagree on what ChunkOffset means, so disambiguate by magnitude: a value
    // smaller than one chunk can only be a chunk index (a float offset of 1 would
    // overlap the previous chunk by 1499 values), anything else is already a float
    // offset. Verified against both cap87 streams.
    size_t chunkStartIndex() const {
        if (chunkOffset <= 0) return 0;
        return (chunkOffset < chunkSize) ? size_t(chunkOffset) * size_t(chunkSize)
                                         : size_t(chunkOffset);
    }
};

// Decode the frame body (the bytes AFTER the 8-byte magic+len prefix) as a
// PluginMeterDataMessage. Returns true and fills `out` if f2 is a valid DataType.
bool parseMeterMessage(const uint8_t* body, size_t len, Update& out);

// Walk every SSL efbc frame in one UDP datagram payload, appending each decoded
// meter message to `out`. Returns the number appended. Malformed / non-meter
// frames are skipped; parsing stops at the first byte that isn't a frame magic.
int parseDatagram(const uint8_t* data, size_t len, std::vector<Update>& out);

} // namespace sslmeter
