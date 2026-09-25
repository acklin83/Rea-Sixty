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

// Level 0x10 reproduces uf1_init_sequence.inc:158 including its 0x65 checksum.
std::vector<uint8_t> buildLedBrightness(uint8_t level) {
    return seal({kFrameMagic, 0x2D, 0x08, 0x00, 0x00,
                 level, 0x00, level, 0x00, level, 0x00});
}

// Level 0x32 reproduces uf1_init_sequence.inc:159 including its 0x83 checksum.
std::vector<uint8_t> buildLcdBrightness(uint8_t level) {
    return seal({kFrameMagic, 0x4F, 0x02, level, 0x00});
}

// Level 0xff reproduces uf1_init_sequence.inc:160 including its 0x47 checksum.
// Proven at the device 2026-09-07; see the header, including why 0x1F is not it
// and why this is a master rather than one display's backlight.
std::vector<uint8_t> buildMasterBrightness(uint8_t level) {
    return seal({kFrameMagic, 0x47, 0x01, level});
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

// ⇨ THE TIME FIELD IS SEGMENT-ADDRESSED, SO IT CAN SPELL (Frank 2026-08-22:
// "können wir das 10-digit Timedisplay missbrauchen um auch Text anzuzeigen?").
// 0x0119 does not take digits — it takes a SEGMENT BITMASK per cell, which is
// why the decode reads (SEG7[d] << 1) | dp. SSL only ever sends the ten digit
// patterns, but nothing in the encoding says it has to: any of the 128 masks is
// a legal cell value, so the field is really eleven little 7-segment canvases.
//
//        aaaa        a = 0x01   d = 0x08   g = 0x40
//       f    b       b = 0x02   e = 0x10
//       f    b       c = 0x04   f = 0x20
//        gggg        The payload byte is (mask << 1) | dp, so the dp (the
//       e    c       separator dot AFTER the cell) is bit 0 and the segments
//       e    c       live in bits 1..7.
//        dddd  dp
//
// ⚠ Four capitals have no 7-segment shape at all — K M V W X. The table below
// approximates them so nothing silently vanishes, but the real rule is to CHOOSE
// WORDS THAT FIT THE FONT: "BARS" reads perfectly, "MIX" never will.
// ✅ HW-PROVEN 2026-08-22: the firmware paints the RAW MASK. It does not look the
// byte up in a digit table — Frank pressed the format step and read letters off
// the panel. The same press proved the field is TEN cells wide, not eleven (see
// kUf1TcFirst below).
uint8_t seg7Glyph(char ch)
{
    switch (ch) {
        // Digits. '9' keeps SSL's own 0x67 (no bottom segment) so a flashed
        // number looks identical to the clock's.
        case '0': return 0x3f; case '1': return 0x06; case '2': return 0x5b;
        case '3': return 0x4f; case '4': return 0x66; case '5': return 0x6d;
        case '6': return 0x7d; case '7': return 0x07; case '8': return 0x7f;
        case '9': return 0x67;
        // Letters that have a real shape. Some are only legible in lower case
        // (b d n r t u), which is the standard calculator alphabet, so the table
        // folds both cases onto the shape that READS rather than the one asked for.
        case 'A': case 'a': return 0x77;
        case 'B': case 'b': return 0x7c;   // lower-case b — an upper B is an 8
        case 'C':           return 0x39;
        case 'c':           return 0x58;
        case 'D': case 'd': return 0x5e;   // lower-case d — an upper D is a 0
        case 'E': case 'e': return 0x79;
        case 'F': case 'f': return 0x71;
        case 'G': case 'g': return 0x3d;
        case 'H':           return 0x76;
        case 'h':           return 0x74;
        case 'I':           return 0x30;   // the left bar, so it is not a 1
        case 'i':           return 0x10;
        case 'J': case 'j': return 0x1e;
        case 'L': case 'l': return 0x38;
        case 'N': case 'n': return 0x54;   // lower-case n — an upper N is an H
        case 'O':           return 0x3f;   // = 0
        case 'o':           return 0x5c;
        case 'P': case 'p': return 0x73;
        case 'Q': case 'q': return 0x67;   // = 9
        case 'R': case 'r': return 0x50;
        case 'S': case 's': return 0x6d;   // = 5
        case 'T': case 't': return 0x78;   // lower-case t
        case 'U':           return 0x3e;
        case 'u': case 'v': return 0x1c;
        case 'Y': case 'y': return 0x6e;
        case 'Z': case 'z': return 0x5b;   // = 2
        // The impossible four, approximated. Say it out loud before shipping a
        // word that needs one of them.
        case 'K': case 'k': return 0x76;   // reads as H
        case 'M': case 'm': return 0x37;   // top box, open at the bottom
        case 'V':           return 0x3e;   // reads as U
        case 'W': case 'w': return 0x3e;   // reads as U
        case 'X': case 'x': return 0x76;   // reads as H
        // Punctuation that costs nothing.
        case '-': return 0x40; case '_': return 0x08; case '=': return 0x48;
        case '\'': return 0x20; case '"': return 0x22; case '?': return 0x53;
        case '[': case '(': return 0x39; case ']': case ')': return 0x0f;
        case '^': return 0x01; case '*': return 0x63;   // degree ring
        case ' ': return 0x00;
    }
    return 0x00;                            // unknown → blank, never garbage
}

// ⛔ TEN CELLS ON THE GLASS, ELEVEN IN THE PAYLOAD. Byte 0 is not displayed —
// HW-PROVEN 2026-08-22, the day the field first got a left-aligned string and
// Frank saw it eat the first letter ("es sind eben NICHT 11!"). Nobody could see
// it before because the clock right-aligns and never filled the field. So every
// writer works in out[kTcFirst .. 10] and leaves out[0] blank.

// Text → the 0x0119 field, LEFT-aligned (text reads left to right; the clock
// right-aligns because a number does). '.' ':' and ',' fold into the dp of the
// cell before them instead of eating one, exactly as the clock's separators do.
// Anything past the ten cells is dropped — there is no scrolling here.
void encodeSeg7Text(const char* s, uint8_t out[11])
{
    for (int k = 0; k < 11; ++k) out[k] = 0x00;
    int n = 0;
    for (const char* p = s; *p && n < kTcCells; ++p) {
        const char c = *p;
        if ((c == '.' || c == ':' || c == ',') && n > 0) {
            out[kTcFirst + n - 1] |= 0x01; continue;
        }
        out[kTcFirst + n++] = static_cast<uint8_t>(seg7Glyph(c) << 1);
    }
}

std::vector<uint8_t> seg7Payload(const std::string& s)
{
    uint8_t cells[11];
    encodeSeg7Text(s.c_str(), cells);
    return std::vector<uint8_t>(cells, cells + 11);
}

} // namespace uf1
