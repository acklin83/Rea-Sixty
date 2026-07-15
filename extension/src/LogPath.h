#pragma once
//
// LogPath — where our diagnostic logs live, per platform.
//
// Every log used to be opened as a hardcoded "/tmp/…". That directory does not
// exist on Windows, so each fopen silently failed and the traces looked dead —
// the frame trace, the stale logs, the init log, all of them. Route every log
// through logPath() instead.
//
// macOS / Linux : /tmp/<name>
// Windows       : %TEMP%\<name>   (GetTempPath, already used for setparam.log)
//
// Deliberately NOT used for /tmp/rea_sixty_udev.rules — that one is Linux-only
// by nature and is paired with a shell command that names /tmp explicitly.

#include <string>

namespace uf8 {

// Absolute path for a diagnostic log file. `filename` is a bare name with no
// directory part, e.g. "rea_sixty.log".
std::string logPath(const char* filename);

}  // namespace uf8
