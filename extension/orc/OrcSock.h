#pragma once
//
// The sockets of ORC's two servers (PhonesServer, SettingsServer) and their tests:
// the handful of calls that differ between the Mac and Windows, one place for both
// (docs/orc-windows-plan.md, section 5).
//
// ⛔ Include before anything that brings windows.h in (memory uf1-sidecar-rme-plan).
// ⛔ Never select() (memory sslcore-fd-setsize-kills-reaper).
// ⛔ No SO_EXCLUSIVEADDRUSE, no SO_REUSEADDR on Windows (memory
// windows-so-reuseaddr-hijacks-port): the default there is what SO_REUSEADDR is on
// the Mac. A live listener on the same address refuses the second bind.

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <arpa/inet.h>
#  include <cerrno>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

#include <cstddef>
#include <vector>

namespace orc::sock {

#if defined(_WIN32)

using sock_t = SOCKET;
inline constexpr sock_t kNone = INVALID_SOCKET;

// Reference counted by Windows; once per process is all ORC needs, and it holds
// until the process ends.
inline void startup()
{
    static const int once = [] { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w); }();
    (void)once;
}
inline void closeSock(sock_t s) { ::closesocket(s); }
inline void setNonBlocking(sock_t s) { u_long on = 1; ::ioctlsocket(s, FIONBIO, &on); }
// No SIGPIPE here: a write to a peer that has gone is an error we read anyway.
inline void noSigPipe(sock_t) {}
inline void allowQuickRebind(sock_t) {}
inline bool wouldBlock()
{
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINTR;
}
inline int pollAll(std::vector<pollfd>& fds, int ms)
{
    return ::WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), ms);
}
inline long sendSome(sock_t s, const char* p, std::size_t n)
{
    return ::send(s, p, static_cast<int>(n), 0);
}
inline long recvSome(sock_t s, char* p, std::size_t n)
{
    return ::recv(s, p, static_cast<int>(n), 0);
}

#else

using sock_t = int;
inline constexpr sock_t kNone = -1;

inline void startup() {}
inline void closeSock(sock_t s) { ::close(s); }
inline void setNonBlocking(sock_t s) { ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL) | O_NONBLOCK); }
// ⛔ A write to a peer that has gone would raise SIGPIPE and end ORC; this turns it
// into an error we read.
inline void noSigPipe(sock_t s)
{
    const int one = 1;
    ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
}
// Past a TIME_WAIT left by the last run, never next to a live listener (that still
// fails with EADDRINUSE).
inline void allowQuickRebind(sock_t s)
{
    const int one = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
}
inline bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
inline int pollAll(std::vector<pollfd>& fds, int ms)
{
    return ::poll(fds.data(), static_cast<nfds_t>(fds.size()), ms);
}
inline long sendSome(sock_t s, const char* p, std::size_t n) { return ::send(s, p, n, 0); }
inline long recvSome(sock_t s, char* p, std::size_t n) { return ::recv(s, p, n, 0); }

#endif

} // namespace orc::sock
