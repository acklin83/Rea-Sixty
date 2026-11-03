#pragma once
//
// The server behind ORC's settings window (docs/orc-settings-web-plan.md).
//
// ⛔ THIS MAC ONLY, AND ONLY WHILE THE WINDOW IS OPEN. It binds 127.0.0.1 on a
// port the system picks, new each time, and runs only between start() and stop().
// So it can never fight MoreMe for a port and nothing listens when nobody looks.
//
// ⛔ NO PIN, BUT A TOKEN NOBODY SEES (Frank 03.11.2026). This line carries the OBS
// password's writer and MoreMe's PIN, so every path holds a random value only the
// window knows, and Host and Origin have to be exactly this address, port included
// (requestAllowed). No other program on this Mac, and no page in a browser, gets in.
//
// One thread, poll() over the listening socket and the connections. ⛔ Never
// select() (memory sslcore-fd-setsize-kills-reaper). The HTTP and the WebSocket
// are orc/WebSocket.cpp, the logic is orc/OrcSettingsModel.cpp.

#include "OrcSettingsModel.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace orc::settings {

// Host and Origin against this server's own address. Origin may be missing (a
// plain page load); when it is there it has to match, port and all. A proxy on
// this Mac (a forwarding header) is refused too.
bool requestAllowed(const std::string& peer, const std::string& xRealIp, const std::string& xForwardedFor,
                    const std::string& host, const std::string& origin, int port);

class Server {
  public:
    Server() = default;
    ~Server();
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    // Starts it and returns the page's address, "" when no socket could be opened.
    // `resources`: the folder holding settings.html and fonts/; ../moreme holds
    // the default logo. Stops what ran before.
    std::string start(const std::string& resources, const Host& host);
    void        stop();
    int         port() const { return port_.load(); }

    // Runs `fn` on the server's thread, which then sends the page the new state.
    void post(std::function<void(Context&)> fn);

  private:
    void loop(int listenFd, std::string token, std::string resources);

    std::thread       worker_;
    std::atomic<bool> run_{false};
    std::atomic<int>  port_{0};
    std::mutex        mx_;
    std::vector<std::function<void(Context&)>> queue_;
    Context           ctx_;   // the server's thread only, once it runs
};

Server& server();

} // namespace orc::settings
