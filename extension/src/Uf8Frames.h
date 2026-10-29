#pragma once
//
// UF8 input: from USB bulk-IN packets to whole frames. Pulled verbatim out of
// main.cpp's onUf8InputUnit_ (29.10.2026, docs/orc-uf8-plan.md, stage 2) so
// that ORC frames the UF8 exactly as Rea-Sixty does, with the October fader
// fixes (split frames, the 31 xx prefix, the position checksum) instead of a
// second copy that would lose them. No REAPER, no device: bytes in, frames out.
// Test: tests/test_uf8_frames.cpp.
//
// Frames on the wire (each starts with FF, then the command byte):
//   FF 04 ..  6 bytes  poll
//   FF 20 02  6 bytes  fader touch (capacitive)
//   FF 21 03  7 bytes  fader position, last byte = sum of the five before it
//   FF 22 03  7 bytes  button
//   FF 23 ..  6 bytes  pressure sensor (TBD)
//   FF 24 02  6 bytes  v-pot rotation
//   FF 33 ..  6 bytes  pressure sensor (TBD)

#include <cstddef>
#include <cstdint>
#include <vector>

namespace uf8frames {

// Carry-over for frames split across USB bulk-IN URBs. UF8 firmware emits
// FF-framed events back-to-back without aligning them to USB packet
// boundaries, so a `FF 20 02 <strip> <state>` touch frame can arrive as
// `FF 20 02` in one URB and `<strip> <state>` in the next. Without stitching,
// both halves are dropped (`unknown:` log) and a fader silently fails to limp
// because the TOUCH event never reaches the dispatcher. Capped to prevent
// unbounded growth from corrupt input.
constexpr std::size_t kResidualMax = 64;

// One per UF8 unit: each unit stitches its own URBs.
struct Assembler {
    std::vector<std::uint8_t> residual;
};

struct Frame {
    std::uint8_t b[8] = {};
    std::uint8_t n    = 0;
};

enum class Skip : std::uint8_t {
    Unknown,       // a byte after FF that is no known command; skipped
    BadChecksum,   // an FF 21 03 position frame whose sum does not match
};

inline std::size_t frameSize(std::uint8_t cmd)
{
    switch (cmd) {
        // FF 04 02 XX 01 XX = 6 bytes total. Two payload variants
        // (02 9d 01 a4 / 02 94 01 9b) cycle alternately. Previous
        // frameSize=7 was wrong: it skipped past the actual end of
        // the poll frame, swallowing whatever FF byte came next —
        // which is exactly how bundled "FF 04 ... FF 21 03 ..."
        // and "FF 04 ... FF 20 02 ..." packets lost their touch /
        // fader events.
        case 0x04: return 6;   // poll
        case 0x21: return 7;   // fader position
        case 0x22: return 7;   // button
        case 0x20: return 6;   // fader touch (capacitive)
        case 0x23: return 6;   // pressure sensor (TBD)
        case 0x24: return 6;   // v-pot rotation
        case 0x33: return 6;   // pressure sensor (TBD)
        default:   return 0;
    }
}

// Splits one URB into whole frames, appended to `out` in arrival order.
// `onSkip(kind, at, avail)` is told about every byte the walk passes over
// (for trace logs and the fader log); it may be a no-op.
template <class OnSkip>
void feed(Assembler& a, const std::uint8_t* dataIn, std::size_t lenIn,
          std::vector<Frame>& out, OnSkip&& onSkip)
{
    std::vector<std::uint8_t>& residual = a.residual;

    // Stitch any leftover bytes from the previous URB to the front of
    // this one before parsing.
    std::vector<std::uint8_t> stitched;
    const std::uint8_t* data = dataIn;
    std::size_t         len  = lenIn;
    if (!residual.empty()) {
        // ⛔ EVERY PACKET STARTS WITH THE 31 xx SESSION PREFIX, the one that
        // continues a split frame too, and sometimes a packet is NOTHING but
        // the prefix (cap51 at 36.038: "31 60 ff 21 03 07 c9", then "31 60",
        // then "31 60 48 3c"). Appended as it came, the prefix became the
        // frame's payload: LSB 0x31 / MSB 0x60 = pb 12337, or MSB 0x31 = 63xx,
        // a one-frame jump of the fader to a fixed value. Two faders at once
        // split more frames, so it showed up there (forum 2026-10-25, and
        // Frank's log the same evening: strip 4 read 12337 and 6295 mid-move).
        const std::uint8_t* cont    = dataIn;
        std::size_t         contLen = lenIn;
        if (contLen >= 2 && cont[0] == 0x31) { cont += 2; contLen -= 2; }
        if (contLen == 0) return;   // prefix only: keep the residual waiting
        stitched.reserve(residual.size() + contLen);
        stitched.insert(stitched.end(), residual.begin(), residual.end());
        stitched.insert(stitched.end(), cont, cont + contLen);
        residual.clear();
        data = stitched.data();
        len  = stitched.size();
    }

    // Walk past each FF frame in the buffer (multiple frames can arrive
    // concatenated, optionally preceded by a 31 60 / 31 00 session prefix).
    std::size_t i = 0;
    while (i < len) {
        // Session-prefix byte 0x31 + flag byte: skip both.
        if (data[i] == 0x31 && i + 1 < len) { i += 2; continue; }
        if (data[i] != 0xFF) { ++i; continue; }
        // Need at least FF + cmd to know how much more to expect. If
        // we only have a lone FF at the end, drop it — it's most likely
        // a stray byte (frame checksum that happened to land on 0xFF),
        // not the start of a real frame. Saving it as residual would
        // mis-frame the next URB. If we have FF+cmd, only save when
        // the cmd byte is one we actually know — otherwise the FF was
        // also a stray.
        if (i + 1 >= len) {
            // Lone FF at the very end — drop.
            break;
        }
        if (i + 2 >= len) {
            // FF + cmd at the end. Only carry over if cmd is recognised.
            const bool known = frameSize(data[i + 1]) != 0;
            if (known && len - i <= kResidualMax) {
                residual.assign(data + i, data + len);
            }
            break;
        }

        const std::uint8_t cmd       = data[i + 1];
        const std::size_t  frameSz   = frameSize(cmd);
        if (frameSz == 0) {
            // Unknown command — report and skip one byte.
            onSkip(Skip::Unknown, data + i, len - i);
            ++i;
            continue;
        }
        if (i + frameSz > len) {
            // Truncated frame at end of buffer — known opcode but the
            // payload extends past the URB boundary. Carry the partial
            // frame into the next URB rather than dropping it (which
            // is how Fader 8 TOUCH events and others were silently lost).
            if (len - i <= kResidualMax) {
                residual.assign(data + i, data + len);
            }
            break;
        }
        if (cmd == 0x21 && data[i + 2] == 0x03) {
            // The last byte is the sum of the five before it (everything after
            // FF), right in all 1287 position frames of the SSL 360 captures.
            // A frame that fails it was put together wrong; resync byte by
            // byte instead of moving a fader to a value nobody sent.
            const std::uint8_t sum = static_cast<std::uint8_t>(
                data[i + 1] + data[i + 2] + data[i + 3] + data[i + 4] + data[i + 5]);
            if (sum != data[i + 6]) {
                onSkip(Skip::BadChecksum, data + i, len - i);
                ++i;
                continue;
            }
        }
        Frame f;
        for (std::size_t k = 0; k < frameSz; ++k) f.b[k] = data[i + k];
        f.n = static_cast<std::uint8_t>(frameSz);
        out.push_back(f);
        i += frameSz;
    }
}

} // namespace uf8frames
