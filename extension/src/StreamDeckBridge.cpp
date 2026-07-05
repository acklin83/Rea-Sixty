#include "StreamDeckBridge.h"

#include "WDL/jsonparse.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <deque>
#include <cstdio>
#include <cstring>

// Socket plumbing mirrors DynaMountClient.cpp (same cross-platform typedefs),
// but this is a LISTENING server rather than a client.
#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
  static constexpr socket_t kInvalid = INVALID_SOCKET;
  #define SD_CLOSE closesocket
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
  #define SD_CLOSE ::close
#endif

namespace uf8::sdbridge {

namespace {

// -------------------------------------------------------------- shared state
std::atomic<bool>  g_running{false};
std::thread        g_worker;
int                g_port = kDefaultPort;

// Inbound: worker parses lines -> queue; main thread drains.
std::mutex             g_inMutex;
std::deque<SdCommand>  g_inQueue;

// Outbound: main thread broadcast() -> queue; worker flushes to clients.
std::mutex                g_outMutex;
std::deque<std::string>   g_outQueue;   // each already newline-terminated

std::atomic<int>   g_clientCount{0};

// ---------------------------------------------------------------- ws startup
void netInit() {
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}
void netCleanup() {
#if defined(_WIN32)
    WSACleanup();
#endif
}

bool setNonBlocking(socket_t s) {
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0) return false;
    return fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0;
#endif
}

bool wouldBlock() {
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

// ------------------------------------------------------------ line parsing
SdCommand parseLine(const char* data, int len) {
    SdCommand c;
    wdl_json_parser p;
    wdl_json_element* root = p.parse(data, len);
    if (!root || !root->is_object()) return c;

    const char* cmd = root->get_string_by_name("cmd", true);
    if (!cmd) return c;

    if (!std::strcmp(cmd, "hello")) {
        c.kind = SdCommand::Kind::Hello;
        if (const char* n = root->get_string_by_name("name", true)) c.name = n;
    } else if (!std::strcmp(cmd, "action")) {
        c.kind = SdCommand::Kind::Action;
        if (const char* n = root->get_string_by_name("name", true)) c.name = n;
        if (const char* pr = root->get_string_by_name("param", true))
            c.param = std::atoi(pr);
    } else if (!std::strcmp(cmd, "reaper")) {
        // id wins over action if both are present.
        if (const char* idv = root->get_string_by_name("id", true)) {
            c.kind = SdCommand::Kind::ReaperId;
            c.id   = std::atoi(idv);
        } else if (const char* a = root->get_string_by_name("action", true)) {
            c.kind = SdCommand::Kind::ReaperName;
            c.name = a;
        }
    } else if (!std::strcmp(cmd, "subscribe")) {
        c.kind = SdCommand::Kind::Subscribe;
    } else if (!std::strcmp(cmd, "list")) {
        c.kind = SdCommand::Kind::List;
    } else if (!std::strcmp(cmd, "meters")) {
        c.kind = SdCommand::Kind::Meters;
        if (auto* arr = root->get_item_by_name("targets");
            arr && arr->is_array() && arr->m_array) {
            const int n = arr->m_array->GetSize();
            for (int i = 0; i < n; ++i) {
                if (auto* el = arr->enum_item(i))
                    if (const char* s = el->get_string_value(true))
                        c.targets.emplace_back(s);
            }
        }
    } else if (!std::strcmp(cmd, "ping")) {
        c.kind = SdCommand::Kind::Ping;
    }
    return c;
}

// ------------------------------------------------------------- client model
struct Client {
    socket_t    fd = kInvalid;
    std::string rx;              // accumulates until a '\n'
    bool        subscribed = false;
};

bool sendRaw(socket_t fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = ::send(fd, data + sent, static_cast<int>(len - sent), 0);
        if (n > 0) { sent += static_cast<size_t>(n); continue; }
        if (n < 0 && wouldBlock()) { std::this_thread::yield(); continue; }
        return false;  // peer closed or hard error
    }
    return true;
}
bool sendLine(socket_t fd, const std::string& line) {
    if (!sendRaw(fd, line.data(), line.size())) return false;
    const char nl = '\n';
    return sendRaw(fd, &nl, 1);
}

// Feed newly-received bytes into a client's buffer; extract complete lines and
// enqueue their parsed commands. Handles Subscribe / Ping locally (they touch
// only this client / this fd). Returns false if the client should be dropped.
bool ingest(Client& cl, const char* buf, int n) {
    cl.rx.append(buf, static_cast<size_t>(n));
    size_t pos;
    while ((pos = cl.rx.find('\n')) != std::string::npos) {
        // Strip an optional trailing '\r' (CRLF-tolerant).
        size_t end = pos;
        if (end > 0 && cl.rx[end - 1] == '\r') --end;
        SdCommand c = parseLine(cl.rx.data(), static_cast<int>(end));
        cl.rx.erase(0, pos + 1);

        switch (c.kind) {
            case SdCommand::Kind::Ping:
                if (!sendLine(cl.fd, "{\"ev\":\"pong\"}")) return false;
                break;
            case SdCommand::Kind::Subscribe:
                cl.subscribed = true;
                // Still surface it to main so it can force a full state push.
                { std::lock_guard<std::mutex> lk(g_inMutex); g_inQueue.push_back(c); }
                break;
            case SdCommand::Kind::Unknown:
                break;  // ignore malformed / unknown
            default:
                { std::lock_guard<std::mutex> lk(g_inMutex); g_inQueue.push_back(c); }
                break;
        }
        // Sanity cap: never let a misbehaving peer grow rx without bound.
        if (cl.rx.size() > 64 * 1024) return false;
    }
    return true;
}

// -------------------------------------------------------------- the worker
void workerMain(int port) {
    netInit();

    socket_t listenFd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenFd == kInvalid) { g_running = false; netCleanup(); return; }

    int yes = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // 127.0.0.1 only

    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0
        || ::listen(listenFd, 8) != 0) {
        SD_CLOSE(listenFd);
        g_running = false;
        netCleanup();
        return;
    }
    setNonBlocking(listenFd);

    std::vector<Client> clients;

    while (g_running.load()) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(listenFd, &rset);
        socket_t maxfd = listenFd;
        for (const auto& c : clients) {
            FD_SET(c.fd, &rset);
            if (c.fd > maxfd) maxfd = c.fd;
        }

        // Short timeout so g_running is polled ~60x/s and outbound lines flush
        // with low latency.
        timeval tv{0, 16 * 1000};
        int sel = ::select(static_cast<int>(maxfd) + 1, &rset, nullptr, nullptr, &tv);

        if (sel > 0) {
            // New connection.
            if (FD_ISSET(listenFd, &rset)) {
                socket_t fd = ::accept(listenFd, nullptr, nullptr);
                if (fd != kInvalid) {
                    setNonBlocking(fd);
                    int one = 1;
                    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                               reinterpret_cast<char*>(&one), sizeof(one));
                    Client cl;
                    cl.fd = fd;
                    // Greet immediately (this thread owns the fd).
                    if (sendLine(fd, "{\"ev\":\"hello\",\"app\":\"Rea-Sixty\",\"proto\":1}")) {
                        clients.push_back(std::move(cl));
                        g_clientCount.store(static_cast<int>(clients.size()));
                    } else {
                        SD_CLOSE(fd);
                    }
                }
            }
            // Reads.
            for (size_t i = 0; i < clients.size();) {
                Client& cl = clients[i];
                bool drop = false;
                if (FD_ISSET(cl.fd, &rset)) {
                    char buf[2048];
                    int n = ::recv(cl.fd, buf, sizeof(buf), 0);
                    if (n > 0)        { drop = !ingest(cl, buf, n); }
                    else if (n == 0)  { drop = true; }              // peer closed
                    else if (!wouldBlock()) { drop = true; }        // hard error
                }
                if (drop) {
                    SD_CLOSE(cl.fd);
                    clients.erase(clients.begin() + static_cast<long>(i));
                    g_clientCount.store(static_cast<int>(clients.size()));
                } else {
                    ++i;
                }
            }
        }

        // Flush outbound broadcast queue to subscribed clients.
        std::deque<std::string> out;
        { std::lock_guard<std::mutex> lk(g_outMutex); out.swap(g_outQueue); }
        if (!out.empty()) {
            for (size_t i = 0; i < clients.size();) {
                Client& cl = clients[i];
                bool drop = false;
                if (cl.subscribed) {
                    for (const auto& line : out) {
                        if (!sendRaw(cl.fd, line.data(), line.size())) { drop = true; break; }
                    }
                }
                if (drop) {
                    SD_CLOSE(cl.fd);
                    clients.erase(clients.begin() + static_cast<long>(i));
                    g_clientCount.store(static_cast<int>(clients.size()));
                } else {
                    ++i;
                }
            }
        }
    }

    for (auto& c : clients) SD_CLOSE(c.fd);
    clients.clear();
    g_clientCount.store(0);
    SD_CLOSE(listenFd);
    netCleanup();
}

} // namespace

// ------------------------------------------------------------------ public
bool start(int port) {
    if (g_running.load()) return true;
    g_port = port;
    g_running.store(true);
    try {
        g_worker = std::thread(workerMain, port);
    } catch (...) {
        g_running.store(false);
        return false;
    }
    return true;
}

void stop() {
    if (!g_running.exchange(false)) {
        if (g_worker.joinable()) g_worker.join();
        return;
    }
    if (g_worker.joinable()) g_worker.join();
    { std::lock_guard<std::mutex> lk(g_inMutex);  g_inQueue.clear(); }
    { std::lock_guard<std::mutex> lk(g_outMutex); g_outQueue.clear(); }
}

bool isRunning() { return g_running.load(); }

int clientCount() { return g_clientCount.load(); }

void drainCommands(std::vector<SdCommand>& out) {
    out.clear();
    std::lock_guard<std::mutex> lk(g_inMutex);
    out.assign(g_inQueue.begin(), g_inQueue.end());
    g_inQueue.clear();
}

void broadcast(const std::string& jsonLine) {
    std::string line = jsonLine;
    line.push_back('\n');
    std::lock_guard<std::mutex> lk(g_outMutex);
    // Cap the backlog so a stalled/absent reader can't grow memory unbounded.
    if (g_outQueue.size() < 256) g_outQueue.push_back(std::move(line));
}

} // namespace uf8::sdbridge
