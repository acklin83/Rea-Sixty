#pragma once
//
// The server half of RFC 6455, and the slice of HTTP/1.1 it arrives through,
// for ORC MoreMe (docs/orc-phones-plan.md, stage 2). Pure: bytes in, bytes out,
// no socket. orc/PhonesServer.cpp owns the sockets; tests/test_orc_websocket.cpp
// pins the frame format.
//
// What the page needs and nothing more (stoerme's src/ws.js did the same):
// text frames, continuation frames put back together, ping answered with pong,
// close. No compression extension, no binary messages, no TLS (a reverse proxy
// in front does TLS, as the NAS does for phones.stoersender.ch).
//
// ⇨ src/WsClient.cpp has the client half with its own framing inside the class.
// Not merged here: that one runs OBS mode, and this change does not touch it.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace orc::ws {

enum Opcode : std::uint8_t {
    kContinuation = 0x0,
    kText         = 0x1,
    kBinary       = 0x2,
    kClose        = 0x8,
    kPing         = 0x9,
    kPong         = 0xA,
};

// A text message bigger than this is a broken or hostile peer: the page's
// biggest message is a few hundred bytes.
inline constexpr std::size_t kMaxMessage = 64 * 1024;

// Sec-WebSocket-Accept for a client's Sec-WebSocket-Key (RFC 6455 4.2.2):
// base64(sha1(key + the RFC's GUID)).
std::string acceptKey(const std::string& clientKey);

// One frame from the server: never masked (5.1), always FIN, never fragmented.
std::string encodeFrame(std::uint8_t opcode, const std::string& payload);

struct Frame {
    std::uint8_t opcode = 0;
    bool         fin    = true;
    std::string  payload;   // unmasked
};

enum class Take {
    Need,    // no whole frame in the buffer yet
    Frame,   // one frame taken off the front of the buffer
    Bad,     // not a frame a client may send: close the connection
};
// Takes one frame off the front of `buf`. ⛔ A client frame without the mask
// bit is Bad (5.1: the server MUST close), and so is one longer than
// kMaxMessage, a control frame longer than 125 bytes or fragmented (5.5), and
// one with reserved bits set (no extension was agreed).
Take takeFrame(std::string& buf, Frame& out);

// Puts fragmented messages back together and answers the control frames.
// Feed it every frame; it says what to do.
class Reader {
  public:
    enum class Got { Nothing, Message, Ping, Close, Bad };
    // `frame` in; on Message `out` holds the text, on Ping the payload to
    // pong back.
    Got feed(const Frame& frame, std::string& out);

  private:
    std::string msg_;
    bool        inMessage_ = false;
    bool        isText_    = false;
};

// ── HTTP ─────────────────────────────────────────────────────────────────────

struct Request {
    std::string method, path;               // path without the query
    std::map<std::string, std::string> headers;   // names lower case, values trimmed
    std::string header(const std::string& lowerName) const
    {
        const auto it = headers.find(lowerName);
        return it == headers.end() ? std::string() : it->second;
    }
};

// The request head, up to and including the blank line. False when it is not
// an HTTP/1.x request line.
bool parseRequest(const std::string& head, Request& out);

// True when `r` asks to become a WebSocket (GET, Upgrade: websocket, a key).
bool isUpgrade(const Request& r);

// The 101 answer for an upgrade request.
std::string upgradeResponse(const Request& r);

// A whole response with a body; the connection closes after it.
std::string response(int status, const std::string& contentType, const std::string& body);

} // namespace orc::ws
