#pragma once
//
// The files ORC and Rea-Sixty hand the surfaces over with, in ORC's folder
// (~/Library/Application Support/ORC). Both programs read and write them, so
// their format lives here once, with a test (tests/test_orc_handover.cpp),
// instead of in two fscanf calls that could drift.
//
//   handover           written by Rea-Sixty while REAPER wants the surfaces.
//                      "<pid>\n" up to Rea-Sixty 0.7.x, "<pid>\nv2\n" from the
//                      Rea-Sixty that waits for ORC's receipt.
//   orc.pid            written by ORC while it runs: "<pid>\n".
//   handover.released  written by ORC once it has let go: "<REAPER pid>\n".
//
// Plan: docs/orc-uf8-plan.md, section 5.

#include <cstdio>
#include <string>

namespace orc_handover {

struct Marker {
    int pid     = 0;   // 0 = no usable marker
    int version = 0;   // 0 none, 1 pid only, 2 with the "v2" line
};

inline std::string formatMarker(int pid)
{
    char b[32];
    snprintf(b, sizeof b, "%d\nv2\n", pid);
    return b;
}

inline Marker parseMarker(const std::string& text)
{
    Marker m;
    int  pid = 0;
    char tag[8] = {};
    const int n = std::sscanf(text.c_str(), "%d %7s", &pid, tag);
    if (n < 1 || pid <= 0) return m;
    m.pid     = pid;
    m.version = (n == 2 && std::string(tag) == "v2") ? 2 : 1;
    return m;
}

// orc.pid and handover.released: a single pid, 0 when there is none.
inline std::string formatPid(int pid)
{
    char b[16];
    snprintf(b, sizeof b, "%d\n", pid);
    return b;
}

inline int parsePid(const std::string& text)
{
    int pid = 0;
    if (std::sscanf(text.c_str(), "%d", &pid) != 1 || pid <= 0) return 0;
    return pid;
}

// Whole small file into a string; empty when it is not there.
inline std::string readFile(const std::string& path)
{
    std::string out;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char buf[256];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        std::fclose(f);
    }
    return out;
}

inline bool writeFile(const std::string& path, const std::string& text)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return ok;
}

} // namespace orc_handover
