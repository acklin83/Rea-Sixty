#pragma once
//
// What ORC's two Windows files (OrcTray.cpp, OrcWebWindow.cpp) share.

#include "OrcConfig.h"

#include <string>
#include <windows.h>

namespace orc {

// In ORC.rc.
constexpr int kIconApp       = 1;
constexpr int kIconTrayWhite = 2;   // for a dark taskbar
constexpr int kIconTrayBlack = 3;   // for a light one

// ORC's strings are UTF-8 (its manifest makes the narrow code page UTF-8 as well).
std::wstring widen(const std::string& s);
std::string  exeFolder();

// ⇨ THE INSTALLED COPY, %ProgramFiles%\ORC. Open at login is only for that one, and
// the menu names the folder of any other: a build folder at every login cost Frank
// four days on the Mac (16.10.2026).
bool runningFromProgramFiles();

} // namespace orc
