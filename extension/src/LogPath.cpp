#include "LogPath.h"

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

}  // namespace uf8
