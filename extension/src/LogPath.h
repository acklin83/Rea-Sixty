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

#include <cstdint>
#include <string>

namespace uf8 {

// Absolute path for a diagnostic log file. `filename` is a bare name with no
// directory part, e.g. "rea_sixty.log".
std::string logPath(const char* filename);

// One line into rea_sixty.log with a controller's USB device revision.
//
// ⛔ bcdDevice IS NOT THE FIRMWARE VERSION. It was logged on 2026-09-10 as
// exactly that, and the same evening's full SSL 360 2.1.12 update — which
// flashes UF8, UF1 and UC1 — left every value unchanged (UF8 0x1000, UF1
// 0x0900), while the UC1 reports the same 0x1000 as the UF8. Two products with
// one value and no movement across a flash is a hardware/product revision,
// not firmware. The firmware version is shown only inside SSL 360 itself.
// The line stays because the revision is still worth having next to the frames
// logged beneath it; it just must not be read as more than it is.
void logDeviceRevision(const char* device, uint16_t bcdDevice,
                       const std::string& serial);

}  // namespace uf8
