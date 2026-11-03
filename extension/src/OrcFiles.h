#pragma once
//
// What the file system does differently where ORC runs, for ORC and for
// Rea-Sixty, which both reach into ORC's folder (OrcHandover.h,
// docs/orc-windows-plan.md, sections 4 and 8). Header only: the extension's tests
// link Bindings.cpp without anything else.
//
// ⛔ THE PATHS ARE NARROW, IN THE PROCESS'S OWN CODE PAGE. Everything above this
// file hands them to fopen. ORC's manifest makes that UTF-8; REAPER's process is
// on its ANSI page. The conversion below follows whichever applies, so one line
// serves both.

#include <string>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <sddl.h>
#  pragma comment(lib, "advapi32.lib")
#else
#  include <cerrno>
#  include <csignal>
#  include <cstdio>
#  include <cstdlib>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace orc_files {

#if defined(_WIN32)

inline std::string narrow_(const wchar_t* w)
{
    const int need = ::WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 1) return {};
    std::string out(static_cast<std::size_t>(need - 1), '\0');
    ::WideCharToMultiByte(CP_ACP, 0, w, -1, &out[0], need, nullptr, nullptr);
    return out;
}

inline std::string appData_()
{
    wchar_t buf[32768];
    const DWORD n = ::GetEnvironmentVariableW(L"APPDATA", buf, 32768);
    return (n > 0 && n < 32768) ? narrow_(buf) : std::string();
}

// ORC's folder, not created. Empty when the system does not say.
inline std::string supportFolder()
{
    const std::string base = appData_();
    return base.empty() ? std::string() : base + "\\ORC";
}

// REAPER's resource folder as an installed REAPER has it.
inline std::string reaperFolder()
{
    const std::string base = appData_();
    return base.empty() ? std::string() : base + "\\REAPER";
}

// Where TotalMix keeps its last.<device>.xml with the snapshot and layout names.
// Read from the StoerPC 03.11.2026: %LOCALAPPDATA%\TotalMixFX.
inline std::string totalMixFolder()
{
    wchar_t buf[32768];
    const DWORD n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, 32768);
    return (n > 0 && n < 32768) ? narrow_(buf) + "\\TotalMixFX" : std::string();
}

inline bool makeDir(const std::string& path) { return ::CreateDirectoryA(path.c_str(), nullptr) != 0; }

inline bool folderExists(const std::string& path)
{
    const DWORD a = ::GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// ⛔ `0600` HAS NO MEANING HERE, AND THE FOLDER BELOW CAN BE READABLE BY EVERYBODY
// (measured on the StoerPC 03.11.2026: Roaming inherits Users and Everyone). ORC's
// folder holds the MoreMe PIN and the Hue and OBS keys, so it gets a list of its
// own when it is made: this user and the system, nothing inherited. Files and
// folders inside inherit it. A folder that is already there is left alone.
inline bool makeOwnerOnlyDir(const std::string& path)
{
    HANDLE tok = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &tok)) return makeDir(path);
    char info[256];
    DWORD got = 0;
    std::string sddl;
    if (::GetTokenInformation(tok, TokenUser, info, sizeof(info), &got)) {
        char* sid = nullptr;
        if (::ConvertSidToStringSidA(reinterpret_cast<TOKEN_USER*>(info)->User.Sid, &sid)) {
            sddl = std::string("D:P(A;OICI;FA;;;") + sid + ")(A;OICI;FA;;;SY)";
            ::LocalFree(sid);
        }
    }
    ::CloseHandle(tok);
    if (sddl.empty()) return makeDir(path);
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl.c_str(), SDDL_REVISION_1,
                                                               &sa.lpSecurityDescriptor, nullptr))
        return makeDir(path);
    const BOOL ok = ::CreateDirectoryA(path.c_str(), &sa);
    ::LocalFree(sa.lpSecurityDescriptor);
    return ok != 0;
}

inline int currentPid() { return static_cast<int>(::GetCurrentProcessId()); }

// ⇨ THE ANSWER kill(pid, 0) GIVES. Access denied is a process that exists and is
// not ours to ask: a REAPER running elevated.
inline bool processAlive(int pid)
{
    if (pid <= 0) return false;
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return ::GetLastError() == ERROR_ACCESS_DENIED;
    DWORD code = 0;
    const BOOL ok = ::GetExitCodeProcess(h, &code);
    ::CloseHandle(h);
    return ok && code == STILL_ACTIVE;
}

// ⛔ fopen OPENS WITHOUT FILE_SHARE_DELETE, so a reader in the other program can
// make replacing or removing a file fail for the moment it holds it. Rea-Sixty
// reads rme.json every second and the handover files are read twice a second, so
// that moment comes. A short retry, then the honest failure.
inline bool transientShareError_(DWORD e)
{
    return e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED || e == ERROR_LOCK_VIOLATION;
}

// Puts `from` in the place of `to` in one step, whether `to` exists or not.
inline bool replaceFile(const std::string& from, const std::string& to)
{
    for (int i = 0; i < 40; ++i) {
        if (::MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
        if (!transientShareError_(::GetLastError())) return false;
        ::Sleep(5);
    }
    return false;
}

// True when this call removed the file. A file that was not there is false.
inline bool removeFile(const std::string& path)
{
    for (int i = 0; i < 40; ++i) {
        if (::DeleteFileA(path.c_str())) return true;
        if (!transientShareError_(::GetLastError())) return false;
        ::Sleep(5);
    }
    return false;
}

// Time and size to tell whether a file changed. ⛔ Not stat(): its time is in
// whole seconds here, and two saves of the same size within one second would look
// like none.
inline bool fileStamp(const std::string& path, long long& mtime, long long& size)
{
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (path.empty() || !::GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &d)) return false;
    mtime = (static_cast<long long>(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime;
    size  = (static_cast<long long>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
    return true;
}

#else

#  if defined(__APPLE__)
inline std::string supportFolder()
{
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/Library/Application Support/ORC" : std::string();
}

inline std::string reaperFolder()
{
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/Library/Application Support/REAPER" : std::string();
}

inline std::string totalMixFolder()
{
    const char* home = std::getenv("HOME");
    return home && *home ? std::string(home) + "/Library/Application Support/RME TotalMix FX" : std::string();
}
#  else
inline std::string supportFolder() { return {}; }   // ORC does not run here
inline std::string reaperFolder() { return {}; }
inline std::string totalMixFolder() { return {}; }
#  endif

inline bool folderExists(const std::string& path)
{
    struct stat sb{};
    return ::stat(path.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode);
}

// An existing directory fails with EEXIST, which is the outcome we want. Any other
// failure shows up as a failed write later, with a path.
inline bool makeDir(const std::string& path) { return ::mkdir(path.c_str(), 0755) == 0; }
inline bool makeOwnerOnlyDir(const std::string& path) { return makeDir(path); }

inline int currentPid() { return static_cast<int>(::getpid()); }

// kill(pid, 0) sends nothing and answers "does it exist": 0, or EPERM for a
// process that exists but is not ours to signal.
inline bool processAlive(int pid)
{
    if (pid <= 0) return false;
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

inline bool replaceFile(const std::string& from, const std::string& to)
{
    return std::rename(from.c_str(), to.c_str()) == 0;
}

inline bool removeFile(const std::string& path) { return std::remove(path.c_str()) == 0; }

inline bool fileStamp(const std::string& path, long long& mtime, long long& size)
{
    struct stat sb{};
    if (path.empty() || ::stat(path.c_str(), &sb) != 0) return false;
#  if defined(__APPLE__)
    mtime = static_cast<long long>(sb.st_mtimespec.tv_sec) * 1000000000LL + sb.st_mtimespec.tv_nsec;
#  else
    mtime = static_cast<long long>(sb.st_mtim.tv_sec) * 1000000000LL + sb.st_mtim.tv_nsec;
#  endif
    size = static_cast<long long>(sb.st_size);
    return true;
}

#endif

} // namespace orc_files
