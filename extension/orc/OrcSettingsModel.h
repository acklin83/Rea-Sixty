#pragma once
//
// ORC's settings as data (docs/orc-settings-web-plan.md): what the page shows,
// and what one message from the page changes. No socket, no window. The page
// computes nothing; the rules live here, and every edit goes through the setters
// the owners of the values already have (rme::manager, the bindings engine, the
// Hue, OBS and DynaMount managers, orc::phones).
//
// ⛔ ONE THREAD: stateJson and apply run on SettingsServer's thread, which has
// the role of the writer the main thread had while the window was AppKit.
// What needs the main thread goes through Host and comes back by post().

#include <functional>
#include <string>
#include <vector>

namespace orc::settings {

// What only the platform can answer. Set by the window (OrcWebWindow.mm).
struct Host {
    std::function<void(bool on)> setLogin;     // open at login
    std::function<void()>        chooseLogo;   // the file dialog; logoChosen with the answer
};

struct Context {
    Host        host;
    bool        loginAvailable = false;        // only for the ORC in Applications
    bool        loginOn        = false;
    int         ownPort        = 0;            // this page's port, not for MoreMe to take
    std::string error;                         // the page's error line
    std::string importResult;
    int         logoStamp      = 0;            // same file name, new picture
};

std::string stateJson(const Context& c);

// One message from the page. Clears the error line and sets it again if needed.
void apply(const std::string& json, Context& c);

// The file dialog's answer.
void logoChosen(const std::string& path, Context& c);

// What a soft key or a transport key can run in ORC: the builtins in the RME and
// Brightness categories, sorted by what the menu shows.
std::vector<std::string> rmeActions();

} // namespace orc::settings
