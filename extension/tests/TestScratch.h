#pragma once
//
// A scratch folder for a test, and ORC's folder inside it, on the Mac and under
// Windows. ⛔ Nothing a test does may reach the real ORC folder or REAPER's.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

namespace testscratch {

// A new empty folder under the system's temp folder, named after `stem`.
inline std::string makeDir(const std::string& stem)
{
    namespace fs = std::filesystem;
    std::random_device rd;
    for (int i = 0; i < 100; ++i) {
        const fs::path p = fs::temp_directory_path() / (stem + std::to_string(rd()));
        std::error_code ec;
        if (fs::create_directory(p, ec) && !ec) return p.string();
    }
    return {};
}

// orc_files::supportFolder() then lands below `dir`.
inline void useAsHome(const std::string& dir)
{
#if defined(_WIN32)
    _putenv_s("APPDATA", dir.c_str());
#else
    ::setenv("HOME", dir.c_str(), 1);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(dir) / "Library/Application Support", ec);
#endif
}

} // namespace testscratch
