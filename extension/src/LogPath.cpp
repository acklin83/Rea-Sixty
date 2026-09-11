#include "LogPath.h"

#include <cstdio>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace uf8 {

std::string logPath(const char* filename)
{
    if (!filename || !*filename) return std::string();
#if defined(_WIN32)
    char tmp[MAX_PATH + 1] = {0};
    // GetTempPathA returns the length and appends a trailing backslash.
    const DWORD n = GetTempPathA(MAX_PATH, tmp);
    if (n == 0 || n > MAX_PATH)
        return std::string("C:\\Windows\\Temp\\") + filename;
    return std::string(tmp) + filename;
#else
    return std::string("/tmp/") + filename;
#endif
}

void logDeviceRevision(const char* device, uint16_t bcdDevice,
                       const std::string& serial)
{
    FILE* lg = std::fopen(logPath("rea_sixty.log").c_str(), "a");
    if (!lg) return;
    std::fprintf(lg, "[usbdev] %s bcdDevice=0x%04X serial=%s\n",
                 device ? device : "?", bcdDevice,
                 serial.empty() ? "-" : serial.c_str());
    std::fclose(lg);
}

}  // namespace uf8
