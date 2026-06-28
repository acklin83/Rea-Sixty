#include "DynaMountClient.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
  static constexpr socket_t kInvalid = INVALID_SOCKET;
  #define DM_CLOSE closesocket
#else
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netinet/tcp.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  using socket_t = int;
  static constexpr socket_t kInvalid = -1;
  #define DM_CLOSE ::close
#endif

namespace uf8::dynamount {

// ============================ pure helpers ===================================

const char* protoName(Proto p) {
    switch (p) {
        case Proto::Gen1Http: return "Gen1";
        case Proto::Gen2Tcp:  return "Gen2";
        case Proto::Offline:  return "Offline";
        default:              return "Unknown";
    }
}

int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

std::string gen1MoveQuery(int h, int r, int v, int s) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "h=%d&r=%d&v=%d&s=%d",
                  clampi(h, kHMin, kHMax),
                  clampi(r, kRMin, kRMax),
                  clampi(v, kVMin, kVMax),
                  s);
    return buf;
}

std::string gen1Request(const std::string& ip, const std::string& query) {
    // Webduino is HTTP/1.0; it closes the socket after the body, which is how
    // we know the response is complete. Keep the request minimal.
    std::string req = "GET /move?";
    req += query;
    req += " HTTP/1.0\r\nHost: ";
    req += ip;
    req += "\r\nConnection: close\r\n\r\n";
    return req;
}

bool isSuccessBody(const std::string& body) {
    return body.find("success") != std::string::npos;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Split an HTTP/1.0 response into status code + trimmed body.
static void parseHttpResponse(const std::string& raw, int& statusOut,
                              std::string& bodyOut) {
    statusOut = 0;
    bodyOut.clear();
    if (raw.empty()) return;
    // Status line: "HTTP/1.0 200 OK"
    size_t sp = raw.find(' ');
    if (sp != std::string::npos)
        statusOut = std::atoi(raw.c_str() + sp + 1);
    // Body is after the blank line.
    size_t hdrEnd = raw.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) hdrEnd = raw.find("\n\n");
    if (hdrEnd != std::string::npos) {
        size_t skip = (raw[hdrEnd] == '\r') ? 4 : 2;
        bodyOut = trim(raw.substr(hdrEnd + skip));
    }
}

// ============================ socket plumbing ================================

void init() {
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

void cleanup() {
#if defined(_WIN32)
    WSACleanup();
#endif
}

static bool setNonBlocking(socket_t s, bool nb) {
#if defined(_WIN32)
    u_long mode = nb ? 1 : 0;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0) return false;
    return fcntl(s, F_SETFL, nb ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK)) == 0;
#endif
}

// Connect to ip:port with a timeout. Returns a connected, blocking socket, or
// kInvalid. A bad/dead IP fails within timeoutMs instead of the OS default.
static socket_t connectTimeout(const std::string& ip, int port, int timeoutMs) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1)
        return kInvalid;

    socket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalid) return kInvalid;

    setNonBlocking(s, true);
    int rc = ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

#if defined(_WIN32)
    bool inProgress = (rc != 0) && (WSAGetLastError() == WSAEWOULDBLOCK);
#else
    bool inProgress = (rc != 0) && (errno == EINPROGRESS);
#endif
    if (rc != 0 && !inProgress) { DM_CLOSE(s); return kInvalid; }

    if (rc != 0) { // wait for writability or timeout
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(s, &wset);
        timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
        int sel = ::select(static_cast<int>(s) + 1, nullptr, &wset, nullptr, &tv);
        if (sel <= 0) { DM_CLOSE(s); return kInvalid; }
        // Confirm there was no async connect error.
        int err = 0;
        socklen_t len = sizeof(err);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR,
                       reinterpret_cast<char*>(&err), &len) != 0 || err != 0) {
            DM_CLOSE(s);
            return kInvalid;
        }
    }
    setNonBlocking(s, false);
    return s;
}

static void setIoTimeout(socket_t s, int timeoutMs) {
#if defined(_WIN32)
    DWORD tv = static_cast<DWORD>(timeoutMs);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv));
#else
    timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

static bool sendAll(socket_t s, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int n = ::send(s, data.data() + sent,
                       static_cast<int>(data.size() - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

// ============================ Gen1 commands =================================

Result gen1SendQuery(const std::string& ip, const std::string& query, int timeoutMs) {
    Result res;
    socket_t s = connectTimeout(ip, kGen1HttpPort, timeoutMs);
    if (s == kInvalid) return res;          // unreachable
    setIoTimeout(s, timeoutMs);

    if (!sendAll(s, gen1Request(ip, query))) { DM_CLOSE(s); return res; }

    std::string raw;
    char buf[1024];
    for (;;) {
        int n = ::recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;                  // 0 = closed by Webduino, <0 = timeout/err
        raw.append(buf, static_cast<size_t>(n));
        if (raw.size() > 64 * 1024) break;  // sanity cap
    }
    DM_CLOSE(s);

    res.reachable = !raw.empty();
    parseHttpResponse(raw, res.status, res.body);
    res.success = res.reachable && isSuccessBody(res.body);
    return res;
}

Result gen1Move(const std::string& ip, int h, int r, int v, int s, int timeoutMs) {
    return gen1SendQuery(ip, gen1MoveQuery(h, r, v, s), timeoutMs);
}

Result gen1Home(const std::string& ip, int s, int timeoutMs) {
    char q[32];
    std::snprintf(q, sizeof(q), "s=%d&rst=%d", s, kRstReturnToHome);
    return gen1SendQuery(ip, q, timeoutMs);
}

Result gen1Calibrate(const std::string& ip, int s, int timeoutMs) {
    char q[32];
    std::snprintf(q, sizeof(q), "s=%d&rst=%d", s, kRstStandardCalibration);
    return gen1SendQuery(ip, q, timeoutMs);
}

Result gen1SetRotationOffset(const std::string& ip, int roff, int s, int timeoutMs) {
    char q[48];
    std::snprintf(q, sizeof(q), "roff=%d&s=%d&rst=%d",
                  clampi(roff, 0, kRoffMax), s, kRstNewRotationOffset);
    return gen1SendQuery(ip, q, timeoutMs);
}

// ============================ detection =====================================

static bool portOpen(const std::string& ip, int port, int timeoutMs) {
    socket_t s = connectTimeout(ip, port, timeoutMs);
    if (s == kInvalid) return false;
    DM_CLOSE(s);
    return true;
}

Proto detectPassive(const std::string& ip, int timeoutMs) {
    if (portOpen(ip, kGen1HttpPort, timeoutMs)) return Proto::Gen1Http;
    if (portOpen(ip, kGen2TcpPort, timeoutMs))  return Proto::Gen2Tcp;
    return Proto::Offline;
}

} // namespace uf8::dynamount
