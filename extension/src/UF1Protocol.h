#pragma once
//
// SSL UF1 wire protocol — frame construction and parsing.
//
// Decoded 2026-06-04 (cap52-83); see docs/protocol-notes-uf1.md and
// docs/uf1-native-build-plan.md. The UF1 protocol is its own dialect — it
// shares the family VID (0x31E9) and the FF-framed shape with the UF8/UC1,
// but the opcodes, value scales and checksum all differ, so none of the UF8
// Protocol.h helpers are reusable here.
//
// Frame (both directions):
//   FF <op> <len> <len payload bytes...> <ck>      total = len + 4
// URBs concatenate multiple frames back-to-back — split on the length byte.
//
// Checksum (both directions):
//   ck = (sum(bytes from FF opcode .. last payload byte) + 1) & 0xFF
// Note the +1 and that the FF opcode IS included — this differs from UF8's
// plain sum-of-payload.
//
// IN stream (EP 0x81): a 2-byte report header `32 <xx>` (0x60 = carries an
// event, 0x00 = idle poll) followed by zero or more concatenated FF frames.
//

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace uf1 {

constexpr uint8_t kFrameMagic = 0xFF;

// ---- Input (EP 0x81) -------------------------------------------------------

enum class InputKind : uint8_t {
    FaderTouch,      // FF 20 02 00 <state>          — capacitive touch on the fader
    FaderPosition,   // FF 21 03 00 <lo> <hi>        — 15-bit LE, 0x0000 bottom .. 0x7FFF top
    EncoderRotate,   // FF 24 02 <id> <delta>        — delta 6-bit signed, CW +, CCW -
    EncoderTouch,    // FF 23 02 <id> <state>        — capacitive touch on a v-pot / channel enc
    Button,          // FF 22 03 <id> 00 <state>     — 01 down / 00 up
};

struct InputEvent {
    InputKind kind;
    uint8_t   id       = 0;   // encoder / button id (0 for the fader)
    bool      pressed  = false; // touch / button state (down == true)
    int       delta    = 0;   // EncoderRotate: signed step count
    uint16_t  position = 0;   // FaderPosition: 15-bit (0..0x7FFF)
};

// Encoder ids (rotate / touch — FF 24 / FF 23). V-pots above-fader + 1..4,
// channel encoder, jog wheel.
namespace enc {
constexpr uint8_t kVpotAboveFader = 0x00;
constexpr uint8_t kVpot1 = 0x01, kVpot2 = 0x02, kVpot3 = 0x03, kVpot4 = 0x04;
constexpr uint8_t kChannel = 0x05;   // channel-nav encoder
constexpr uint8_t kJog     = 0x06;   // jog wheel (rotate only, no touch/push)
}

// Button / push ids (FF 22 03 <id>). Push ids (v-pot / channel-enc presses)
// live in 0x08..0x0D; the rest are physical buttons.
namespace btn {
// V-pot + channel-encoder pushes
constexpr uint8_t kVpotAboveFaderPush = 0x08;
constexpr uint8_t kVpot1Push = 0x09, kVpot2Push = 0x0A, kVpot3Push = 0x0B, kVpot4Push = 0x0C;
constexpr uint8_t kChannelPush = 0x0D;
// Soft keys + channel buttons
constexpr uint8_t kChannelSoftKey = 0x18;
constexpr uint8_t kDisplaySoft1 = 0x19, kDisplaySoft2 = 0x1A, kDisplaySoft3 = 0x1B, kDisplaySoft4 = 0x1C;
constexpr uint8_t kSolo = 0x1D, kCut = 0x1E, kSel = 0x1F;
// Nav block 0x20..0x27 (even = top row, odd = bottom row)
constexpr uint8_t kMode = 0x20, kBankLeft = 0x21, k5to8 = 0x22, kBankRight = 0x23;
constexpr uint8_t kArrowLeft = 0x24, k360 = 0x25, kArrowRight = 0x26, kScrub = 0x27;
// NAV cross 0x28..0x2C
constexpr uint8_t kNavUp = 0x28, kNavLeft = 0x29, kNavCentre = 0x2A, kNavRight = 0x2B, kNavDown = 0x2C;
// Transport soft-keys 0x30..0x36
constexpr uint8_t kTLeft = 0x30, kTRight = 0x31, kCycle = 0x32, kClick = 0x33, kT1 = 0x34, kT2 = 0x35, kShift = 0x36;
// Flip / Master / transport 0x38..0x3E
constexpr uint8_t kFlip = 0x38, kMaster = 0x39, kRwd = 0x3A, kFfw = 0x3B, kStop = 0x3C, kPlay = 0x3D, kRec = 0x3E;
}

using InputHandler = std::function<void(const InputEvent&)>;

// Parse a raw IN buffer (one bulk-read payload): walk EVERY FF frame, skipping
// any non-FF header bytes between them (the `32 60` report header AND inner
// headers when SSL batches several messages in one URB), invoking `cb` for each
// recognised event. Frames with a bad length or checksum are skipped (resync to
// the next 0xFF). Returns the number of bytes CONSUMED — `data[returned..]` is an
// incomplete tail split by the URB boundary, which the caller must keep as a
// residual and prepend to the next transfer. Pure; safe on the worker thread.
size_t parseInputStream(std::span<const uint8_t> data, const InputHandler& cb);

// ---- Output (EP 0x02) ------------------------------------------------------

// Checksum over the frame bytes from the FF opcode through the last payload
// byte (exclusive of the checksum slot itself): (sum + 1) & 0xFF.
uint8_t checksum(std::span<const uint8_t> frameWithoutCk);

// Colour any element (button LED, SEL track-colour element, EQ graph, ...):
//   FF 38 04 <id> 00 <XX> <YY> <ck>
//   XX = (g4 << 4) | r4 ,  YY = 0xF0 | b4         (4-bit per channel, gamma-ish)
// g4/r4/b4 are the 4-bit (0..15) per-channel levels.
std::vector<uint8_t> buildColour(uint8_t id, uint8_t g4, uint8_t r4, uint8_t b4);

// Convenience: quantise a 0x00RRGGBB to the UF1's 4-bit GRB and build the
// colour frame for `id`.
std::vector<uint8_t> buildColourRgb(uint8_t id, uint32_t rgb);

// Quantise an 8-bit channel to the UF1's 4-bit scale. Exposed for callers
// that want the raw nibbles (e.g. to compare against captured ground-truth).
uint8_t quantiseChannel(uint8_t v8);

// LED on/off (mono path):
//   FF 3B 03 <led_id> 00 <state> <ck>            state 0x01 on / 0x00 off
std::vector<uint8_t> buildLed(uint8_t ledId, bool on);

// LED brightness/colour level (the active toggle path, post-init):
//   FF 39 04 <led_id> 00 <level> 0xF0 <ck>
// `level` is a combined brightness+colour-index byte (see buildLedLevel impl
// and led::lvl* below).
// IMPORTANT (cap64): the channel button LEDs need the FF38 PRIMARY companion
// (buildLedPrimary) sent alongside this FF39 — FF39 alone is inert. cap65's
// "FF39-only toggles Cut" was an incomplete capture read (it missed the FF38).
std::vector<uint8_t> buildLedLevel(uint8_t ledId, uint8_t level);

// FF38 primary companion to buildLedLevel. Same {id,00,level,F0} body, opcode
// 0x38. cap64 proves both frames are required to paint a button LED — SSL 360
// sends FF38 then FF39, both carrying the level on the lit transition. In the
// resting/off state the two diverge: FF38 carries a per-LED dim byte (Solo
// 0xef, Cut 0x3f — see led::kPrim*Off) while FF39 carries 0x00.
std::vector<uint8_t> buildLedPrimary(uint8_t ledId, uint8_t level);

// Fader motor enable / release:
//   FF 1D 02 00 <01|00> <ck>                     01 engage (tracks host) / 00 limp
std::vector<uint8_t> buildMotorEnable(bool enable);

// Fader motor target position:
//   FF 1E 03 00 <lo> <hi> <ck>                   15-bit LE, 0x0000 bottom .. 0x7FFF top
std::vector<uint8_t> buildMotorPosition(uint16_t pos15);

// Screen element write:
//   FF 67 <len> <addrHi> <addrLo> <payload...> <ck>     len = payload.size() + 2
// Writes payload bytes to the 16-bit element address (see protocol-notes-uf1).
std::vector<uint8_t> buildScreen(uint16_t elementAddr, std::span<const uint8_t> payload);

// Plugin-Mode keepalive (worker streams this ~every 150 ms; counter cycles 0..3):
//   FF 1B 01 <ctr> <ck>
std::vector<uint8_t> buildKeepalive(uint8_t counter);

// LED / colour element ids (FF 38 / FF 3B). Solo/Cut/Sel mirror their buttons;
// Sel (0x07) doubles as the track-colour element. More ids appear in the init
// sweep (docs/protocol-notes-uf1).
namespace led {
constexpr uint8_t kSolo = 0x05, kCut = 0x06, kSel = 0x07;
constexpr uint8_t kEqGraph = 0x03;   // EQ-graph tint (via "EQ Colour")

// FF38 colour byte = (green_nibble << 4) | red_nibble, yy=0xF0 (no blue). These
// are REAL RGB nibbles, NOT opaque primaries — Frank 2026-08-05: the old 0xef/0x3f
// showed YELLOW / ORANGE on HW because they carried a stray green nibble (0xef=g14
// r15=yellow, 0x3f=g3 r15=orange). Pure green/red isolate the nibbles.
//   LIT  (soloed/muted):  FF38 = <colour>, FF39 = 0x00
//   DIM/off:              FF38 = FF39 = <dim colour>
constexpr uint8_t kPrimSoloLit = 0xf0;  // g=15,r=0  → pure bright green
constexpr uint8_t kPrimCutLit  = 0x0f;  // g=0,r=15  → pure bright red
constexpr uint8_t kFf39Lit     = 0x00;  // FF39 carries 0x00 on the lit transition
constexpr uint8_t kDimSolo     = 0x20;  // g=2,r=0   → dim green (resting)
constexpr uint8_t kDimCut      = 0x02;  // g=0,r=2   → dim red (resting, never fully off)
}

// Screen element addresses (FF 67) — the subset we drive natively first.
// Full map in docs/protocol-notes-uf1 / docs/uf1-native-build-plan.
namespace scr {
// Channel-info zone (0x00xx)
// The soft-key label above the CHANNEL display (the small LCD). One byte of
// index (always 0 — there is one such key) then the text. Found in our own
// init replay as `ff 67 09 00 04 00 'BYPASS'`: we shipped SSL's captured
// default and never drove it again, which is why every channel read BYPASS
// regardless of the binding. Distinct from kSoftKeyLabel (0x0104), which is
// the BIG display's four keys. Frank 2026-08-10.
constexpr uint16_t kChSoftKey = 0x0004;
// Backdrop BEHIND that soft-key label — the highlight that says "engaged".
// Found by the 2026-08-10 hardware sweep (nothing in 265 captures pointed at it,
// because it carries no text). 2 bytes; the init leaves it at {0x03, 0x00} =
// normal, and 0x01 lights it. Lets a stateful SOFT binding show its state
// WITHOUT overwriting the label, which belongs to the user.
constexpr uint16_t kChSoftKeyBg = 0x0000;
constexpr uint16_t kChActive  = 0x0006;  // 1-byte "channel populated" flag (01) — gates the colour bar
constexpr uint16_t kColourBar = 0x0018;  // 1-byte palette index of the fader colour bar (uf8::quantize)
constexpr uint16_t kTrackName = 0x000b;
constexpr uint16_t kOutputDb  = 0x000c;
constexpr uint16_t kPanLabel  = 0x000e;
constexpr uint16_t kPanBar    = 0x000f;
constexpr uint16_t kChNumber  = 0x0014;
constexpr uint16_t kCsType    = 0x0017;
// Large LCD (0x01xx)
constexpr uint16_t kTimecode     = 0x0119;  // 11-char 7-segment time/position field
                                            // (Channel view only). byte = (SEG7[d]<<1)|dp,
                                            // dp = separator dot after the digit; 0x00 = blank.
                                            // Decoded 2026-07-24, analysis/uf1_0119_timecode_decode.py.
constexpr uint16_t kSoftKeyLabel = 0x0104;
constexpr uint16_t kFocusedParam = 0x010e;  // single focused param text (name+value)
constexpr uint16_t kVpotBars     = 0x010f;  // 4 V-pot readout bars
constexpr uint16_t kHeaderRow    = 0x011c;  // Channel view: 8 x 25-byte ASCII header
                                            // (REAPER | N/8 | OFF). Meter view: 6 x 25-byte
                                            // dB readout grid [PkHold|PkL|PkR|RmsHold|RmsL|RmsR].
                                            // Same address, content re-purposed by firmware view.
constexpr uint16_t kGraphic      = 0x0122;  // EQ graph (Channel) / meter graphic (Meter view)
// Solo-Active indication — the region under the firmware's "SOLO CLR 1" caption
// (manual p187). 1 byte, SSL's own values: 0x00 off / 0xff on. It is a FLAG, not
// a text field: non-zero makes the firmware draw its own "SOLO ACTIVE", so the
// wording is not ours to choose. Found by the 2026-08-10 hardware sweep; the
// corpus holds only two 0xff frames (cap55, cap64), which is why every search
// for the ASCII failed.
constexpr uint16_t kSoloActive   = 0x0120;
}
// NB: 0x011d is NOT a view selector (disproven — cap84=0x11, cap77=0x19, cap66=0xfb;
// too variable, streaming it switched nothing). The Channel<->Meter view is toggled
// by the MODE button (btn::kMode) on the device itself; the host does not command it
// (cap75: no switch opcode, only content re-paints). Do not reintroduce a kViewMode.

} // namespace uf1
