#include "RmeNames.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include "OrcFiles.h"

#if defined(__APPLE__) || defined(_WIN32)
#include <filesystem>
#include <system_error>
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

std::string toUtf8(const std::string& s)
{
    return isMultibyteUtf8(s) ? s : latin1ToUtf8(s);
}

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

std::string stateFileKey(const std::string& deviceName)
{
    std::string k;
    for (const char ch : deviceName)
        if (ch != ' ' && ch != '(' && ch != ')') k.push_back(ch);
    return k;
}

std::string shownDevice(const std::string& defaultXml)
{
    std::string v;
    return findVal(defaultXml, "DeviceName", v) ? v : std::string();
}

std::string pickStateFile(const std::vector<StateFile>& files, const std::string& device)
{
    const std::string key = stateFileKey(device);
    if (!key.empty()) {
        const std::string want = "last." + key + ".xml";
        for (const auto& f : files)
            if (f.name == want) return f.name;
    }
    std::string best;
    long long bestT = -1;
    for (const auto& f : files)
        if (f.mtime > bestT) { bestT = f.mtime; best = f.name; }
    return best;
}

const Names& namesFromDisk()
{
    static Names s_names;
#if defined(__APPLE__) || defined(_WIN32)
    static std::string s_path;
    static long long   s_mtime = -1;
    static auto        s_next  = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now < s_next) return s_names;
    s_next = now + std::chrono::seconds(2);

    const std::string dir = orc_files::totalMixFolder();
    if (dir.empty()) return s_names;
    // One last.*.xml per device TotalMix has seen. The one of the device its
    // window shows (pickStateFile); the newest only when that cannot be told.
    std::vector<StateFile> files;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string nm = it->path().filename().string();
        if (nm.size() < 9 || nm.compare(0, 5, "last.") != 0
            || nm.compare(nm.size() - 4, 4, ".xml") != 0)
            continue;
        long long t = 0, size = 0;
        if (!orc_files::fileStamp(dir + "/" + nm, t, size)) continue;
        files.push_back({ nm, t });
    }
    std::string device;
    {
        std::ifstream d(dir + "/rme.totalmix.default.xml", std::ios::binary);
        if (d) {
            std::stringstream ds;
            ds << d.rdbuf();
            device = shownDevice(ds.str());
        }
    }
    const std::string pick = pickStateFile(files, device);
    if (pick.empty()) return s_names;
    const std::string best = dir + "/" + pick;
    long long bestT = -1;
    for (const auto& f : files)
        if (f.name == pick) bestT = f.mtime;
    if (best == s_path && bestT == s_mtime) return s_names;
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
