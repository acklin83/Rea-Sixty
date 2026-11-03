#pragma once
//
// MoreMe's performers (docs/moreme-performers-plan.md): who hears what. A
// performer has a link of their own, an output, faders and what they may touch.
// The sound engineer edits them on Studio Control, the page at /desk.
//
// Pure, as orc/Phones.cpp: no socket, no thread, no file. The server reads and
// writes moreme-performers.json with these strings (orc/PhonesServer.cpp), and
// it is the only writer of that file, so the settings tab (moreme.json) never
// races an edit from the desk.
//

#include "RmeState.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace orc::phones {

struct Fader {
    std::string src;      // "in:3", "pb:5"
    std::string name;     // "" = TotalMix' name
    std::string colour;   // "#rrggbb", "" = TotalMix' colour
};

struct Performer {
    std::string id;       // the link: /p/<id>. Never changes
    std::string name, role;
    std::string output;   // a spec as on the UF8's keys: "phones1", "output:8"
    std::string colour;   // the swatch in the lists
    bool useLayout = true;   // TotalMix' visible channels; false: `faders`
    std::vector<Fader> faders;
    // ⇨ THE OWN OUTPUT changes only these headphones.
    bool master = true, masterEq = false, masterDyn = false, masterAuto = false;
    // ⇨ A CHANNEL changes for everyone hearing it, and the recording: locked
    // from the factory. 0 off, 1 simple, 2 advanced.
    int  chanEq = 0, chanDyn = 0;
    bool fxSend = false;
    // May hide and sort faders on the phone. Kept on the phone.
    bool hide = false;
};

struct Book {
    std::vector<Performer> performers;
    std::string current;   // the name of the snapshot in use, "" = none
    std::map<std::string, std::vector<Performer>> snapshots;
};

std::string bookJson(const Book& b);
bool        parseBook(const std::string& json, Book& out);

// A link id: lower case letters and digits, 4 to 16. Anything else from the
// network never gets as far as a lookup.
bool validToken(const std::string& t);

// One message from Studio Control. True when the book changed. `token` makes a
// new link id.
bool applyDesk(Book& b, const std::string& json, const std::function<std::string()>& token);

// What an output may be: the roles TotalMix has assigned, then the visible
// outputs without a role (rmeu::outputsWithoutRole, the rule of ORC's menus).
std::string outputsJson(const reasixty::rme::State& st);

// ⛔ STUDIO CONTROL IS FOR THE LOCAL NETWORK. The peer has to be private, no
// reverse proxy in between (no forwarding header), and the Host and Origin have
// to be a local name: a page on the internet cannot talk to it through the
// engineer's browser.
bool deskAllowed(const std::string& peer, const std::string& xRealIp, const std::string& xForwardedFor,
                 const std::string& host, const std::string& origin);

} // namespace orc::phones
