#include "RmeNames.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#if defined(__APPLE__)
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace reasixty::rme {

namespace {

// True when `s` is well-formed UTF-8 AND holds at least one multi-byte
// sequence. Plain ASCII is the same in both encodings and needs nothing.
bool isMultibyteUtf8(const std::string& s)
{
    bool multi = false;
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        int len = 0;
        if      (c < 0x80)           len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else return false;
        if (i + static_cast<size_t>(len) > s.size()) return false;
        for (int k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) & 0xC0) != 0x80)
                return false;
        if (len > 1) multi = true;
        i += static_cast<size_t>(len);
    }
    return multi;
}

std::string latin1ToUtf8(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 4);
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x80) {
            out.push_back(ch);
        } else {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

std::string unescapeXml(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out.push_back(s[i]); continue; }
        static const struct { const char* ent; char ch; } kEnt[] = {
            { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' },
            { "&quot;", '"' }, { "&apos;", '\'' },
        };
        bool hit = false;
        for (const auto& e : kEnt) {
            const size_t n = std::strlen(e.ent);
            if (s.compare(i, n, e.ent) == 0) {
                out.push_back(e.ch);
                i += n - 1;
                hit = true;
                break;
            }
        }
        if (!hit) out.push_back('&');
    }
    return out;
}

// The v="..." of the <val e="<key>" .../> element, or false.
bool findVal(const std::string& xml, const std::string& key, std::string& out)
{
    const std::string needle = "e=\"" + key + "\"";
    const size_t at = xml.find(needle);
    if (at == std::string::npos) return false;
    const size_t v = xml.find("v=\"", at + needle.size());
    const size_t end = xml.find('>', at);
    if (v == std::string::npos || (end != std::string::npos && v > end)) return false;
    const size_t q = xml.find('"', v + 3);
    if (q == std::string::npos) return false;
    std::string raw = unescapeXml(xml.substr(v + 3, q - (v + 3)));
    out = isMultibyteUtf8(raw) ? raw : latin1ToUtf8(raw);
    return true;
}

std::string fallback(const char* what, int i)
{
    char b[24];
    std::snprintf(b, sizeof(b), "%s %d", what, i + 1);
    return b;
}

}  // namespace

bool parseNames(const std::string& xml, Names& out)
{
    Names n;
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        char k[24];
        std::snprintf(k, sizeof(k), "SnapshotName %d", i);
        any |= findVal(xml, k, n.snapshot[i]);
        std::snprintf(k, sizeof(k), "LayoutName %d", i);
        any |= findVal(xml, k, n.layout[i]);
    }
    if (any) out = n;
    return any;
}

std::string snapshotName(const Names& n, int i)
{
    if (i < 0 || i >= 8) return std::string();
    return n.snapshot[i].empty() ? fallback("Snapshot", i) : n.snapshot[i];
}

std::string layoutName(const Names& n, int i)
{
    if (i < 0 || i >= 8) return std::string();
    return n.layout[i].empty() ? fallback("Layout", i) : n.layout[i];
}

const Names& namesFromDisk()
{
    static Names s_names;
#if defined(__APPLE__)
    static std::string s_path;
    static long long   s_mtime = -1;
    static auto        s_next  = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now < s_next) return s_names;
    s_next = now + std::chrono::seconds(2);

    const char* home = std::getenv("HOME");
    if (!home || !*home) return s_names;
    const std::string dir = std::string(home)
                          + "/Library/Application Support/RME TotalMix FX";
    // The newest last.*.xml: one per device TotalMix has seen, and the one it
    // wrote last belongs to the device that is running (Frank 22.09.).
    std::string best;
    long long   bestT = -1;
    if (DIR* d = opendir(dir.c_str())) {
        while (const dirent* e = readdir(d)) {
            const std::string nm = e->d_name;
            if (nm.size() < 9 || nm.compare(0, 5, "last.") != 0
                || nm.compare(nm.size() - 4, 4, ".xml") != 0)
                continue;
            const std::string p = dir + "/" + nm;
            struct stat st{};
            if (stat(p.c_str(), &st) != 0) continue;
            const long long t = static_cast<long long>(st.st_mtime);
            if (t > bestT) { bestT = t; best = p; }
        }
        closedir(d);
    }
    if (best.empty() || (best == s_path && bestT == s_mtime)) return s_names;
    s_path  = best;
    s_mtime = bestT;
    std::ifstream f(best, std::ios::binary);
    if (!f) return s_names;
    std::stringstream ss;
    ss << f.rdbuf();
    Names fresh;
    if (parseNames(ss.str(), fresh)) s_names = fresh;
#endif
    return s_names;
}

}  // namespace reasixty::rme
