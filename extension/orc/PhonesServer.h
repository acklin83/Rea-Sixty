#pragma once
//
// ORC MoreMe's server: the web page, the WebSocket, and the second TotalMix
// remote behind them (docs/orc-phones-plan.md, stages 2 and 3).
//
// One thread, poll() over the listening socket and every connection, 50 ms at
// most between turns. ⛔ Never select(): FD_SETSIZE once took a whole REAPER
// down (memory sslcore-fd-setsize-kills-reaper); poll() has no such ceiling.
// The mixer's logic is orc/Phones.cpp and knows no socket.
//
// ⛔ ITS OWN TOTALMIX REMOTE, AND IT STAYS. The surfaces talk to TotalMix
// through rme::manager() (Remote 3 on Frank's rig), and that link closes when
// REAPER takes the surfaces over (orc/Surface.cpp, the handover). The musicians
// are mid-take exactly then. So MoreMe has its own rme::Manager on its own ports
// (Remote 2, 7003/7004 from the factory), and nothing in the handover touches it.
//
// ⛔ OFF FROM THE FACTORY (Frank 29.10.2026): no port, no remote, no thread,
// until the box on the MoreMe page is ticked. ⛔ A PIN AS SOON AS IT IS ON:
// generated on the first switch-on, four digits, never in the QR code.

#include "RmeManager.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace orc::phones {

// moreme.json in ORC's folder. The PIN is not in it (moreme.pin, readable by
// the user only).
struct Settings {
    bool        enabled  = false;
    int         webPort  = 8088;
    int         sendPort = 7003;   // TotalMix listens here (its "Port incoming")
    int         recvPort = 7004;   // TotalMix answers here (its "Port outgoing")
    std::string studio;            // shown on the page; empty = no name
    std::string logo;              // a file name in ORC's folder; empty = the MoreMe mark
    std::array<std::string, 4> names;   // Phones 1-4 on the page; "" = TotalMix' name
};

bool        loadSettings(Settings& out);   // false: no file yet (defaults stay)
bool        saveSettings(const Settings& s);
std::string settingsPath();
std::string loadPin();                     // "" = none yet
bool        savePin(const std::string& pin);
std::string generatePin();                 // four digits

// Copies a picture into ORC's folder as the page's logo and returns the file
// name to keep in Settings::logo, or "" with `err` set. PNG, JPG and SVG.
std::string importLogo(const std::string& sourcePath, std::string& err);

struct Status {
    bool        enabled   = false;
    bool        listening = false;
    int         port      = 0;
    std::string error;        // why the page is not served, "" when it is
    reasixty::rme::LinkState link = reasixty::rme::LinkState::Off;
    bool        tmSeen    = false;
    int         phones    = 0;   // past the PIN
    int         held      = 0;   // headphones in use
};

class Server {
  public:
    Server() = default;
    ~Server();
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    // Takes the settings and the PIN as they are now: starts, stops or
    // reconfigures. Main thread.
    void apply(const Settings& s, const std::string& pin);
    void stop();

    Status status() const;
    // What apply() was last given: the one copy the settings page reads.
    Settings    settings() const;
    std::string pin() const;

  private:
    void loop();

    mutable std::mutex mx_;
    Settings           want_;
    std::string        pin_;
    std::uint32_t      gen_ = 0;
    Status             status_;

    std::thread        worker_;
    std::atomic<bool>  run_{false};

    reasixty::rme::Manager link_;   // the second remote
};

Server& server();

// The menu's line: "off", "TotalMix does not answer", "2 phones" ...
std::string summary(const Status& st);

// http://<this Mac's address>:<port>, the address musicians type or scan. ""
// when the Mac has no network address.
std::string pageAddress(int port);

} // namespace orc::phones
