#pragma once
//
// What ORC's menu says, for the menu bar and the taskbar alike. ⛔ The texts live
// here once: tools/orc_manual.py reads them from this file, and two menus with two
// copies would drift (Learning #51).

#include <string>

namespace orc {

class Surface;

struct MenuLines {
    std::string uf1, uf8, mixer, moreme;
    bool        deskShown = false;   // Studio Control: only while the page is served
    std::string address;             // the page, empty while it is not served
    std::string addressLine;         // the address and the PIN, for the musicians
};

// `surface` may be null: the first lines then stay as bare names.
MenuLines menuLines(const Surface* surface);

} // namespace orc
