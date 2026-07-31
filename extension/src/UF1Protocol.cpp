#include "UF1Protocol.h"

#include <cmath>

namespace uf1 {

uint8_t checksum(std::span<const uint8_t> frameWithoutCk) {
    unsigned sum = 0;
    for (uint8_t b : frameWithoutCk) sum += b;
    return static_cast<uint8_t>((sum + 1) & 0xFF);
}

namespace {

// Append the trailing checksum for a frame whose bytes (FF..last payload) are
// already in `f`, then return it. Keeps every builder one line.
std::vector<uint8_t> seal(std::vector<uint8_t> f) {
    f.push_back(checksum(f));
    return f;
}

// Dispatch a single FF frame (op/len already read) to an InputEvent.
// `payload` is the `len` payload bytes (excludes FF/op/len/ck). Returns false
// if the opcode isn't a recognised input event.
bool decodeFrame(uint8_t op, std::span<const uint8_t> payload, InputEvent& ev) {
    switch (op) {
        case 0x20:  // fader touch: payload = [00, state]
            if (payload.size() < 2) return false;
            ev.kind = InputKind::FaderTouch;
            ev.id = 0;
            ev.pressed = (payload[1] != 0);
            return true;
        case 0x21:  // fader position: payload = [00, lo, hi]
            if (payload.size() < 3) return false;
            ev.kind = InputKind::FaderPosition;
            ev.id = 0;
            ev.position = static_cast<uint16_t>(payload[1] | (payload[2] << 8));
            return true;
        case 0x22:  // button / push: payload = [id, 00, state]
            if (payload.size() < 3) return false;
            ev.kind = InputKind::Button;
            ev.id = payload[0];
            ev.pressed = (payload[2] != 0);
            return true;
        case 0x23:  // encoder touch: payload = [id, state]
            if (payload.size() < 2) return false;
            ev.kind = InputKind::EncoderTouch;
            ev.id = payload[0];
            ev.pressed = (payload[1] != 0);
            return true;
        case 0x24: {  // encoder rotate: payload = [id, delta] (6-bit signed mod 0x40)
            if (payload.size() < 2) return false;
            ev.kind = InputKind::EncoderRotate;
            ev.id = payload[0];
            int d = payload[1] & 0x3F;
            if (d >= 0x20) d -= 0x40;   // sign-extend 6-bit
            ev.delta = d;
            return true;
        }
        default:
            return false;
    }
}

}  // namespace

size_t parseInputStream(std::span<const uint8_t> data, const InputHandler& cb) {
    size_t i = 0;
    const size_t n = data.size();
    while (i < n) {
        // Skip any non-FF byte. This eats the leading `32 60` report header AND
        // any INNER header between concatenated messages in one URB — SSL batches
        // closely-timed events (each with its own 32 60 prefix), and the old loop
        // stopped at the first inner 32 60 and dropped every event after it. That
        // was the "Shift+button must be pressed/released SIMULTANEOUSLY" bug
        // (Frank 2026-07-31): sequential presses land as two 32-60 messages in one
        // URB, and only the first survived.
        if (data[i] != kFrameMagic) { ++i; continue; }
        // At an FF frame start — need the 3-byte header, then the body + checksum.
        if (i + 3 > n) break;                                // partial header at the tail
        const uint8_t op  = data[i + 1];
        const uint8_t len = data[i + 2];
        const size_t total = static_cast<size_t>(len) + 4;   // FF op len <len> ck
        if (i + total > n) break;                            // frame split by the URB edge
        // Verify checksum; on mismatch resync to the next 0xFF (top of the loop).
        const uint8_t ck = data[i + total - 1];
        if (checksum(data.subspan(i, total - 1)) != ck) { ++i; continue; }
        InputEvent ev{};
        if (decodeFrame(op, data.subspan(i + 3, len), ev)) cb(ev);
        i += total;
    }
    // Bytes consumed. data[i..] is the incomplete tail (a header/frame split by
    // the URB boundary) — the caller keeps it as a residual and prepends it to the
    // next URB so a frame straddling two transfers isn't lost.
    return i;
}

// ---- Output builders -------------------------------------------------------

uint8_t quantiseChannel(uint8_t v8) {
    // 8-bit -> 4-bit with a gamma-ish curve. cap70 ground-truth: orange's
    // green (0x80) quantises to nibble 3, which a plain v8>>4 (= 8) misses;
    // gamma 2.2 lands it (0.502^2.2 * 15 ~= 3). Endpoints 0/255 -> 0/15 for
    // any gamma. Exact SSL curve still TBD — refine in Phase 1 if needed.
    const float f = std::pow(v8 / 255.0f, 2.2f) * 15.0f;
    int n = static_cast<int>(f + 0.5f);
    if (n < 0) n = 0; else if (n > 15) n = 15;
    return static_cast<uint8_t>(n);
}

std::vector<uint8_t> buildColour(uint8_t id, uint8_t g4, uint8_t r4, uint8_t b4) {
    const uint8_t xx = static_cast<uint8_t>(((g4 & 0x0F) << 4) | (r4 & 0x0F));
    const uint8_t yy = static_cast<uint8_t>(0xF0 | (b4 & 0x0F));
    return seal({kFrameMagic, 0x38, 0x04, id, 0x00, xx, yy});
}

std::vector<uint8_t> buildColourRgb(uint8_t id, uint32_t rgb) {
    const uint8_t r = (rgb >> 16) & 0xFF;
    const uint8_t g = (rgb >> 8) & 0xFF;
    const uint8_t b = rgb & 0xFF;
    return buildColour(id, quantiseChannel(g), quantiseChannel(r), quantiseChannel(b));
}

std::vector<uint8_t> buildLed(uint8_t ledId, bool on) {
    return seal({kFrameMagic, 0x3B, 0x03, ledId, 0x00, static_cast<uint8_t>(on ? 0x01 : 0x00)});
}

std::vector<uint8_t> buildLedLevel(uint8_t ledId, uint8_t level) {
    // FF 39 04 <id> 00 <level> 0xF0 <ck>. The trailing 0xF0 is the constant
    // upper-colour byte (no blue) seen for the Solo/Cut LEDs in cap64/cap65;
    // `level` carries brightness+colour-index (cap65 Cut ground truth: 0x12
    // bright red / 0x00 dim red; cap64 Solo: 0x11 green / 0x00 off).
    return seal({kFrameMagic, 0x39, 0x04, ledId, 0x00, level, 0xF0});
}

std::vector<uint8_t> buildLedPrimary(uint8_t ledId, uint8_t level) {
    // FF 38 04 <id> 00 <level> 0xF0 <ck>. The FF38 primary companion to
    // buildLedLevel — cap64 shows both frames are required to paint a button
    // LED (FF38 then FF39, same level on the lit transition).
    return seal({kFrameMagic, 0x38, 0x04, ledId, 0x00, level, 0xF0});
}

std::vector<uint8_t> buildMotorEnable(bool enable) {
    return seal({kFrameMagic, 0x1D, 0x02, 0x00, static_cast<uint8_t>(enable ? 0x01 : 0x00)});
}

std::vector<uint8_t> buildMotorPosition(uint16_t pos15) {
    pos15 &= 0x7FFF;
    return seal({kFrameMagic, 0x1E, 0x03, 0x00,
                 static_cast<uint8_t>(pos15 & 0xFF),
                 static_cast<uint8_t>((pos15 >> 8) & 0xFF)});
}

std::vector<uint8_t> buildScreen(uint16_t elementAddr, std::span<const uint8_t> payload) {
    const uint8_t len = static_cast<uint8_t>(payload.size() + 2);  // 2 addr bytes + payload
    std::vector<uint8_t> f{kFrameMagic, 0x67, len,
                           static_cast<uint8_t>((elementAddr >> 8) & 0xFF),
                           static_cast<uint8_t>(elementAddr & 0xFF)};
    f.insert(f.end(), payload.begin(), payload.end());
    return seal(std::move(f));
}

std::vector<uint8_t> buildKeepalive(uint8_t counter) {
    return seal({kFrameMagic, 0x1B, 0x01, static_cast<uint8_t>(counter & 0x03)});
}

} // namespace uf1
