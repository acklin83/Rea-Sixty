#pragma once
//
// A WebSocket client, the smallest one that can hold a conversation with
// obs-websocket. RFC 6455, text frames only.
//
// There is no WebSocket anywhere else in this tree and no library to lean on:
// reasixty::http cannot do an HTTP Upgrade, and vendoring one costs the
// notarisation and ReaPack price the project has refused to pay (HttpClient.h).
// So: raw sockets, the same connect-with-timeout plumbing DynaMountClient uses,
// and about as much framing as the protocol demands and no more.
//
// What it does:
//   · the Upgrade handshake, with a random Sec-WebSocket-Key
//   · client-to-server MASKING, which the RFC requires and every server enforces
//   · text frames, continuation frames reassembled into one message
//   · ping answered with pong, close answered and reported
//
// What it deliberately does not do: TLS (obs-websocket is plaintext and this
// talks to 127.0.0.1), compression extensions, binary messages, or a message
// bigger than kMaxMessage — a 4 MB scene list is a broken peer, not a big one.
//
// One thread at a time. The OBS worker owns it; nothing else touches it.

#include <cstdint>
#include <string>

namespace reasixty::ws {

// The most a single message may be before the connection is dropped as broken.
inline constexpr size_t kMaxMessage = 4u * 1024u * 1024u;

class Client {
  public:
    Client() = default;
    ~Client();
    Client(const Client&)            = delete;
    Client& operator=(const Client&) = delete;

    // TCP connect + Upgrade handshake. `host` is a dotted IPv4 or a name; the
    // name is resolved with getaddrinfo. false leaves nothing open.
    bool connect(const std::string& host, int port, const std::string& path,
                 int timeoutMs);

    // Masked text frame. false = the peer is gone; the caller should close.
    bool sendText(const std::string& s);

    // Reads for up to timeoutMs.
    //   1  a complete text message is in `out`
    //   0  nothing yet, still connected
    //  -1  closed or broken; call close()
    int poll(std::string& out, int timeoutMs);

    void close();
    bool connected() const;

    // Why the last connect() or poll() failed, for the status line.
    const std::string& lastError() const { return err_; }

  private:
    bool handshake(const std::string& host, int port, const std::string& path,
                   int timeoutMs);
    bool sendFrame(uint8_t opcode, const std::string& payload);
    int  readSome(int timeoutMs);            // socket → rx_
    bool takeFrame(std::string& out, bool& isText, bool& fin, uint8_t& opcode);

    long long   sock_ = -1;    // socket_t widened; -1 = closed on every platform
    std::string rx_;           // bytes read, not yet decoded
    std::string msg_;          // message being reassembled across continuations
    bool        msgIsText_ = false;
    std::string err_;
};

}  // namespace reasixty::ws
