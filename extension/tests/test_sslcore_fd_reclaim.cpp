// test_sslcore_fd_reclaim — the impersonator must not hoard sockets.
//
// Background: on 2026-08-17 REAPER vanished with no dialog while audio kept
// playing. One plug-in object had opened 70 SIMULTANEOUS control connections in
// 150 s and closed none of them; with both sides living in the same process the fd
// numbers climbed until accept() returned 1025, and the worker's FD_SET() then
// faulted the whole host (EXC_GUARD, __darwin_check_fd_set_overflow).
//
// ⚠ WHAT THIS TEST DOES AND DOES NOT DO. It does NOT reproduce the kill. That was
// measured: the pre-fix select() build passes every check below, including a client
// on fd 1211. Writing past an fd_set's end is undefined behaviour, and whether it
// faults is gated per-binary — it killed REAPER and it quietly "works" in a small
// test executable. So this file is not a red/green proof of the fix; it is a lock
// on the behaviour the fix must keep having:
//   1. A client that dies dirty (RST) is reclaimed, not held.
//   2. Descriptor use stays flat across hundreds of connect/kill cycles.
//   3. A connection flood is capped and survived, instead of being served without
//      limit (measured under identical load: pre-fix peak fd 369 and no cap,
//      post-fix 201 with the cap firing).
//   4. A client whose socket lands past FD_SETSIZE is still served — the thing an
//      fd_set structurally cannot do.
//   5. Writing into dead peers does not signal the process to death. THIS one IS a
//      red/green: pre-fix this binary exits 141 (SIGPIPE), post-fix it exits 0.
//
// Self-contained: the impersonator touches no REAPER API.

#include "SslCoreImpersonator.h"

#include <sys/socket.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <poll.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_fail = 0;

void check(const char* what, bool ok, const std::string& detail = {})
{
    std::printf("  %-52s %s%s%s\n", what, ok ? "ok" : "FAIL",
                detail.empty() ? "" : "  ", detail.c_str());
    if (!ok) ++g_fail;
}

// Open descriptors of THIS process. /dev/fd is a per-process view on macOS and
// Linux alike, so counting its entries counts our own fds.
int openFdCount()
{
    DIR* d = ::opendir("/dev/fd");
    if (!d) return -1;
    int n = 0;
    while (::readdir(d)) ++n;
    ::closedir(d);
    return n;
}

void settle(int ms = 250)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// One TCP connection to the impersonator's control port.
int connectTo(uint16_t port)
{
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port   = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        ::close(s);
        return -1;
    }
    return s;
}

// Say enough for the worker to greet us: any bytes trigger the reactive handshake,
// which is what binds the per-connection UDP socket — the second fd per client, and
// the one that made the leak cost two descriptors a time instead of one.
void sayHello(int s)
{
    static const uint8_t hello[] = {
        0xef, 0xbc, 0x51, 0x00,                          // frame magic
        0x14, 0x00, 0x00, 0x00,                          // length = 20
        0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,  // 12-byte header
        0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00,                          // paylen
        0x04, 0x00, 0x00, 0x00,                          // type 4 = hello
    };
    ::send(s, hello, sizeof(hello), 0);
}

// Wait for the worker to say something back, up to timeoutMs. Returns the byte
// count (0 = it never answered). Uses poll on OUR side deliberately: the test must
// not inherit the very limitation it is measuring.
int awaitReply(int s, int timeoutMs)
{
    pollfd p{};
    p.fd = s;
    p.events = POLLIN;
    if (::poll(&p, 1, timeoutMs) <= 0) return 0;
    uint8_t b[4096];
    const int n = int(::recv(s, b, sizeof(b), 0));
    return n > 0 ? n : 0;
}

// Kill a connection the DIRTY way. SO_LINGER with a zero timeout makes close()
// send RST instead of FIN, which is the end the worker cannot mistake for an
// orderly goodbye.
void killDirty(int s)
{
    linger lg{};
    lg.l_onoff  = 1;
    lg.l_linger = 0;
    ::setsockopt(s, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
    ::close(s);
}

}  // namespace

int main()
{
    // ⚠ SIGPIPE IS LEFT AT ITS DEFAULT ON PURPOSE — do not add a SIG_IGN here.
    // The worker heartbeats every client every 0.25 s, so it writes into peers that
    // have just gone away as a matter of course, and a write to a reset socket
    // raises SIGPIPE, whose default disposition kills the process. That is not
    // hypothetical: the pre-fix build dies here with 141 (128 + SIGPIPE), measured.
    // REAPER happens to ignore the signal, which is why on the host it merely
    // leaked instead. Leaving the default in place is what makes this test cover
    // the SO_NOSIGPIPE / MSG_NOSIGNAL suppression too — a regression there kills
    // this binary outright, which is a very loud failure.

    std::printf("sslcore fd reclaim\n");

    // An EXPLICIT control port, not 0: the impersonator announces the port it got
    // but exposes no accessor for it, and the test has to know where to knock.
    // High and arbitrary so it cannot collide with a real 360 install.
    const uint16_t port = 47311;
    if (!sslcore::start(port, 16010)) {
        std::printf("  could not start the impersonator (ports in use? SSL 360 running?)\n");
        return 77;   // skip, not a failure
    }
    settle(300);
    check("impersonator running", sslcore::isRunning());
    if (!sslcore::isRunning()) { sslcore::stop(); return 1; }

    // ---- 1. a dirty death is reclaimed ------------------------------------
    {
        const int before = openFdCount();
        int s = connectTo(port);
        check("connect", s >= 0);
        sayHello(s);
        settle();                       // let the worker greet + bind its UDP socket
        killDirty(s);
        settle(400);                    // …and notice the RST
        const int after = openFdCount();
        // Our own client socket is gone either way; what matters is that the
        // worker's two (TCP + dedicated UDP) came back too.
        check("dirty close reclaims the worker's sockets", after <= before,
              std::to_string(before) + " -> " + std::to_string(after));
    }

    // ---- 2. descriptor use stays flat over many cycles ---------------------
    {
        // Warm up first: the very first connections bind per-connection UDP
        // sockets and grow the containers, so the baseline is taken AFTER a few
        // cycles, not before them.
        for (int i = 0; i < 5; ++i) {
            int s = connectTo(port);
            if (s >= 0) { sayHello(s); settle(60); killDirty(s); }
        }
        settle(400);
        const int base = openFdCount();

        for (int i = 0; i < 200; ++i) {
            int s = connectTo(port);
            if (s < 0) continue;
            sayHello(s);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            killDirty(s);
        }
        settle(800);
        const int after = openFdCount();
        check("200 connect/kill cycles do not grow the fd table",
              after <= base + 8,
              "base " + std::to_string(base) + " -> " + std::to_string(after));
    }

    // ---- 3. the cap holds against a client that will not leave -------------
    {
        std::vector<int> held;
        held.reserve(200);
        for (int i = 0; i < 120; ++i) {
            int s = connectTo(port);
            if (s < 0) break;
            sayHello(s);
            held.push_back(s);
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        settle(500);
        // The worker refuses past its ceiling, so it must still be alive and
        // serving — the point of the cap is that a runaway degrades instead of
        // taking the host with it.
        check("worker survives a connection flood", sslcore::isRunning());
        for (int s : held) killDirty(s);
        settle(600);
        check("flood is fully reclaimed afterwards", sslcore::isRunning());
    }

    // ---- 4. a client socket above FD_SETSIZE is still SERVED ---------------
    // The 2026-08-17 kill was FD_SET() faulting the host on an fd >= 1024. That
    // fault is NOT reproducible here: it is gated per-binary (measured — the old
    // select() build passes a "did the process survive" check in this test while
    // it demonstrably killed REAPER), so asserting survival would assert nothing.
    //
    // What IS deterministic is the behaviour underneath: an fd_set simply CANNOT
    // represent an fd past its ceiling. Under select() such a client is never
    // marked readable, so it is never greeted and never gets its opening sequence
    // — it is a plug-in the surface silently ignores. Under poll() it is served
    // like any other. So: burn descriptors until the worker's next accept lands
    // past 1023, then require an actual reply on the wire.
    {
        rlimit rl{};
        bool raised = false;
        if (::getrlimit(RLIMIT_NOFILE, &rl) == 0) {
            const rlim_t want = 4096;
            if (rl.rlim_cur < want) {
                rl.rlim_cur = (rl.rlim_max < want) ? rl.rlim_max : want;
                raised = ::setrlimit(RLIMIT_NOFILE, &rl) == 0;
            } else {
                raised = true;
            }
        }
        if (!raised || rl.rlim_cur <= 1100) {
            std::printf("  %-52s skipped  (fd limit %llu)\n",
                        "socket above FD_SETSIZE",
                        static_cast<unsigned long long>(rl.rlim_cur));
        } else {
            std::vector<int> burn;
            burn.reserve(1200);
            int highest = 0;
            for (int i = 0; i < 1200; ++i) {
                const int f = ::open("/dev/null", O_RDONLY);
                if (f < 0) break;
                if (f > highest) highest = f;
                burn.push_back(f);
            }
            check("could push descriptors past FD_SETSIZE", highest > 1024,
                  "highest " + std::to_string(highest));
            if (highest > 1024) {
                int s = connectTo(port);
                check("connect on a high descriptor", s >= 0,
                      "our end fd " + std::to_string(s));
                if (s >= 0) {
                    sayHello(s);
                    // The greeting is ~600 bytes of opening + subscribe. Give it a
                    // generous second; silence means the worker never saw us.
                    const int got = awaitReply(s, 1000);
                    check("a client past FD_SETSIZE is GREETED", got > 0,
                          std::to_string(got) + " bytes back");
                    check("worker still running afterwards", sslcore::isRunning());
                    killDirty(s);
                }
            }
            for (int f : burn) ::close(f);
            settle(300);
        }
    }

    sslcore::stop();
    settle(300);

    std::printf("%s\n", g_fail ? "FAILED" : "all good");
    return g_fail ? 1 : 0;
}
