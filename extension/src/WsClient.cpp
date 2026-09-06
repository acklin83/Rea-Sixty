#include "WsClient.h"

#include <cstring>
#include <random>
#include <string>

#if defined(_WIN32)
  // ⛔ winsock2.h BEFORE anything that drags in windows.h, or the old winsock.h
  // wins and every symbol collides (same trap as StreamDeckBridge.cpp).
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
  static constexpr socket_t kInvalidSock = INVALID_SOCKET;
  #define WS_CLOSE closesocket
  #define WS_ERRNO WSAGetLastError()
  #define WS_WOULDBLOCK WSAEWOULDBLOCK
#else
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <netdb.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  using socket_t = int;
  static constexpr socket_t kInvalidSock = -1;
  #define WS_CLOSE ::close
  #define WS_ERRNO errno
  #define WS_WOULDBLOCK EWOULDBLOCK
#endif

#include "Sha256.h"

namespace reasixty::ws {
namespace {

void setNonBlocking(socket_t s, bool on)
{
#if defined(_WIN32)
    u_long m = on ? 1u : 0u;
    ioctlsocket(s, FIONBIO, &m);
#else
    int f = fcntl(s, F_GETFL, 0);
    if (f < 0) return;
    fcntl(s, F_SETFL, on ? (f | O_NONBLOCK) : (f & ~O_NONBLOCK));
#endif
}

// Connect with a deadline, so a dead host fails in timeoutMs instead of the
// OS default. Same shape as DynaMountClient::connectTimeout, plus getaddrinfo
// because OBS may sit on another machine and be named rather than numbered.
socket_t connectTimeout(const std::string& host, int port, int timeoutMs)
{
    char portStr[16];
    std::snprintf(portStr, sizeof(portStr), "%d", port);

    addrinfo hints{};
    hints.ai_family   = AF_INET;          // obs-websocket binds v4 by default
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res)
        return kInvalidSock;

    socket_t s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == kInvalidSock) { freeaddrinfo(res); return kInvalidSock; }

    setNonBlocking(s, true);
    const int rc = ::connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen));
    freeaddrinfo(res);

    const bool inProgress =
#if defined(_WIN32)
        (rc != 0) && (WS_ERRNO == WSAEWOULDBLOCK);
#else
        (rc != 0) && (errno == EINPROGRESS);
#endif
    if (rc != 0 && !inProgress) { WS_CLOSE(s); return kInvalidSock; }

    if (rc != 0) {
        fd_set w;
        FD_ZERO(&w);
        FD_SET(s, &w);
        timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
        if (::select(static_cast<int>(s) + 1, nullptr, &w, nullptr, &tv) <= 0) {
            WS_CLOSE(s);
            return kInvalidSock;
        }
        int err = 0;
        socklen_t len = sizeof(err);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR,
                       reinterpret_cast<char*>(&err), &len) != 0 || err != 0) {
            WS_CLOSE(s);
            return kInvalidSock;
        }
    }
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<char*>(&one), sizeof(one));
    return s;                      // stays non-blocking; poll() does the waiting
}

bool sendAll(socket_t s, const char* p, size_t n)
{
    size_t sent = 0;
    while (sent < n) {
        const int w = ::send(s, p + sent, static_cast<int>(n - sent), 0);
        if (w > 0) { sent += static_cast<size_t>(w); continue; }
#if defined(_WIN32)
        if (w < 0 && WS_ERRNO == WSAEWOULDBLOCK) {
#else
        if (w < 0 && (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)) {
#endif
            fd_set wr;
            FD_ZERO(&wr);
            FD_SET(s, &wr);
            timeval tv{ 2, 0 };
            if (::select(static_cast<int>(s) + 1, nullptr, &wr, nullptr, &tv) <= 0)
                return false;
            continue;
        }
        return false;
    }
    return true;
}

std::string randomKey()
{
    unsigned char raw[16];
    std::random_device rd;
    for (int i = 0; i < 16; i += 4) {
        const uint32_t v = rd();
        raw[i + 0] = static_cast<unsigned char>(v & 0xFF);
        raw[i + 1] = static_cast<unsigned char>((v >> 8) & 0xFF);
        raw[i + 2] = static_cast<unsigned char>((v >> 16) & 0xFF);
        raw[i + 3] = static_cast<unsigned char>((v >> 24) & 0xFF);
    }
    return base64Encode(raw, sizeof(raw));
}

}  // namespace

Client::~Client() { close(); }

bool Client::connected() const { return sock_ != -1; }

void Client::close()
{
    if (sock_ != -1) {
        WS_CLOSE(static_cast<socket_t>(sock_));
        sock_ = -1;
    }
    rx_.clear();
    msg_.clear();
    msgIsText_ = false;
}

bool Client::connect(const std::string& host, int port, const std::string& path,
                     int timeoutMs)
{
    close();
    err_.clear();
#if defined(_WIN32)
    // Winsock may already be up (the Stream Deck bridge starts first), and a
    // second WSAStartup is refcounted, so this is safe either way.
    { WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa); }
#endif
    const socket_t s = connectTimeout(host, port, timeoutMs);
    if (s == kInvalidSock) { err_ = "no answer at " + host; return false; }
    sock_ = static_cast<long long>(s);
    if (!handshake(host, port, path, timeoutMs)) { close(); return false; }
    return true;
}

bool Client::handshake(const std::string& host, int port, const std::string& path,
                       int timeoutMs)
{
    const std::string key = randomKey();
    std::string req;
    req  = "GET " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + ":" + std::to_string(port) + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + key + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n\r\n";
    if (!sendAll(static_cast<socket_t>(sock_), req.data(), req.size())) {
        err_ = "handshake could not be sent";
        return false;
    }

    // Read until the header terminator. The server's Sec-WebSocket-Accept is not
    // verified: it proves the peer speaks the protocol, and the status line
    // already says that. On loopback there is nobody to impersonate.
    std::string head;
    const int deadlineMs = timeoutMs;
    int waited = 0;
    while (head.find("\r\n\r\n") == std::string::npos) {
        const int n = readSome(50);
        if (n < 0) { err_ = "connection closed during handshake"; return false; }
        if (n == 0) {
            waited += 50;
            if (waited >= deadlineMs) { err_ = "no handshake answer"; return false; }
            continue;
        }
        head = rx_;
        if (head.size() > 16384) { err_ = "handshake answer far too long"; return false; }
    }
    const size_t end = head.find("\r\n\r\n");
    const std::string status = head.substr(0, head.find("\r\n"));
    if (status.find(" 101") == std::string::npos) {
        err_ = "server refused the upgrade: " + status;
        return false;
    }
    rx_.erase(0, end + 4);         // anything after the header is already frames
    return true;
}

int Client::readSome(int timeoutMs)
{
    if (sock_ == -1) return -1;
    const socket_t s = static_cast<socket_t>(sock_);
    fd_set r;
    FD_ZERO(&r);
    FD_SET(s, &r);
    timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    const int sel = ::select(static_cast<int>(s) + 1, &r, nullptr, nullptr, &tv);
    if (sel < 0) return -1;
    if (sel == 0) return 0;
    char buf[4096];
    const int n = ::recv(s, buf, sizeof(buf), 0);
    if (n == 0) return -1;                     // orderly close
    if (n < 0) {
#if defined(_WIN32)
        return (WS_ERRNO == WSAEWOULDBLOCK) ? 0 : -1;
#else
        return (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) ? 0 : -1;
#endif
    }
    rx_.append(buf, static_cast<size_t>(n));
    return n;
}

// Pull ONE frame out of rx_ if a whole one is there. Returns false when more
// bytes are needed (rx_ untouched).
bool Client::takeFrame(std::string& out, bool& isText, bool& fin, uint8_t& opcode)
{
    if (rx_.size() < 2) return false;
    const unsigned char b0 = static_cast<unsigned char>(rx_[0]);
    const unsigned char b1 = static_cast<unsigned char>(rx_[1]);
    fin    = (b0 & 0x80) != 0;
    opcode = static_cast<uint8_t>(b0 & 0x0F);
    const bool masked = (b1 & 0x80) != 0;      // never set by a server
    uint64_t len = b1 & 0x7F;
    size_t need = 2;
    if (len == 126) {
        if (rx_.size() < 4) return false;
        len = (uint64_t(static_cast<unsigned char>(rx_[2])) << 8)
            |  uint64_t(static_cast<unsigned char>(rx_[3]));
        need = 4;
    } else if (len == 127) {
        if (rx_.size() < 10) return false;
        len = 0;
        for (int i = 0; i < 8; ++i)
            len = (len << 8) | uint64_t(static_cast<unsigned char>(rx_[2 + i]));
        need = 10;
    }
    const size_t maskLen = masked ? 4u : 0u;
    if (len > kMaxMessage) { err_ = "frame far too long"; return false; }
    if (rx_.size() < need + maskLen + len) return false;

    unsigned char mask[4] = {0, 0, 0, 0};
    if (masked) for (int i = 0; i < 4; ++i)
        mask[i] = static_cast<unsigned char>(rx_[need + i]);

    out.assign(rx_, need + maskLen, static_cast<size_t>(len));
    if (masked)
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = static_cast<char>(static_cast<unsigned char>(out[i]) ^ mask[i % 4]);

    rx_.erase(0, need + maskLen + static_cast<size_t>(len));
    isText = (opcode == 0x1);
    return true;
}

int Client::poll(std::string& out, int timeoutMs)
{
    if (sock_ == -1) return -1;

    // Try what is already buffered first: one recv can carry several frames.
    for (int guard = 0; guard < 64; ++guard) {
        std::string payload;
        bool isText = false, fin = false;
        uint8_t op = 0;
        if (!takeFrame(payload, isText, fin, op)) break;

        switch (op) {
            case 0x9:                                     // ping → pong, same body
                sendFrame(0xA, payload);
                continue;
            case 0xA:                                     // pong, nothing to do
                continue;
            case 0x8:                                     // close
                sendFrame(0x8, std::string());
                err_ = "OBS closed the connection";
                return -1;
            case 0x0:                                     // continuation
                msg_ += payload;
                break;
            case 0x1:                                     // text
            case 0x2:                                     // binary, kept for the guard below
                msg_ = payload;
                msgIsText_ = (op == 0x1);
                break;
            default:
                err_ = "unknown frame type";
                return -1;
        }
        if (msg_.size() > kMaxMessage) { err_ = "message far too long"; return -1; }
        if (!fin) continue;
        if (!msgIsText_) { msg_.clear(); continue; }      // binary: not our language
        out.swap(msg_);
        msg_.clear();
        return 1;
    }

    const int n = readSome(timeoutMs);
    if (n < 0) { if (err_.empty()) err_ = "connection lost"; return -1; }
    if (n == 0) return 0;

    // Something arrived; decode it on this call rather than making the caller
    // come back, so a message never waits a whole loop pass for no reason.
    return poll(out, 0);
}

bool Client::sendFrame(uint8_t opcode, const std::string& payload)
{
    if (sock_ == -1) return false;
    std::string f;
    f += static_cast<char>(0x80 | opcode);              // FIN + opcode
    const size_t n = payload.size();
    if (n < 126) {
        f += static_cast<char>(0x80 | n);               // MASK + length
    } else if (n <= 0xFFFF) {
        f += static_cast<char>(0x80 | 126);
        f += static_cast<char>((n >> 8) & 0xFF);
        f += static_cast<char>(n & 0xFF);
    } else {
        f += static_cast<char>(0x80 | 127);
        for (int i = 7; i >= 0; --i)
            f += static_cast<char>((uint64_t(n) >> (8 * i)) & 0xFF);
    }
    // ⛔ Client frames MUST be masked (RFC 6455 §5.3). A server is required to
    // drop the connection on an unmasked one, and obs-websocket does.
    unsigned char mask[4];
    {
        std::random_device rd;
        const uint32_t v = rd();
        mask[0] = static_cast<unsigned char>(v & 0xFF);
        mask[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
        mask[2] = static_cast<unsigned char>((v >> 16) & 0xFF);
        mask[3] = static_cast<unsigned char>((v >> 24) & 0xFF);
    }
    f.append(reinterpret_cast<char*>(mask), 4);
    const size_t off = f.size();
    f.append(payload);
    for (size_t i = 0; i < n; ++i)
        f[off + i] = static_cast<char>(static_cast<unsigned char>(f[off + i]) ^ mask[i % 4]);
    return sendAll(static_cast<socket_t>(sock_), f.data(), f.size());
}

bool Client::sendText(const std::string& s) { return sendFrame(0x1, s); }

}  // namespace reasixty::ws
