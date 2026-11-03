#include "OrcConfig.h"

#include "RmeManager.h"
#include "OrcFiles.h"
#include "OrcHandover.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#if defined(_WIN32)
#  include <bcrypt.h>
#  pragma comment(lib, "bcrypt.lib")
#elif defined(__APPLE__)
#  include <climits>
#  include <mach-o/dyld.h>
#endif

namespace orc {

std::string supportDir()
{
    const std::string d = orc_files::supportFolder();
    if (d.empty()) return ".";
    // Best effort: an existing directory is the outcome we want. A failure here
    // shows up as a failed write later, with a path.
    orc_files::makeOwnerOnlyDir(d);
    return d;
}

std::string resourcesDir()
{
#if defined(_WIN32)
    wchar_t exe[32768];
    const DWORD n = ::GetModuleFileNameW(nullptr, exe, 32768);
    if (n == 0 || n >= 32768) return {};
    std::string p = orc_files::narrow_(exe);          // ...\\ORC\\ORC.exe
    return p.substr(0, p.find_last_of('\\'));
#else
    char exe[PATH_MAX];
    std::uint32_t size = sizeof(exe);
    if (_NSGetExecutablePath(exe, &size) != 0) return {};
    char real[PATH_MAX];
    if (!::realpath(exe, real)) return {};
    std::string p = real;                             // .../ORC.app/Contents/MacOS/ORC
    p = p.substr(0, p.find_last_of('/'));             // .../Contents/MacOS
    p = p.substr(0, p.find_last_of('/'));             // .../Contents
    return p + "/Resources";
#endif
}

std::string version()
{
#ifdef ORC_VERSION
    return ORC_VERSION;
#else
    return "?";
#endif
}

void randomBytes(void* out, std::size_t n)
{
#if defined(_WIN32)
    ::BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), static_cast<ULONG>(n),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    arc4random_buf(out, n);
#endif
}

unsigned randomBelow(unsigned n)
{
#if defined(_WIN32)
    // arc4random_uniform's way: throw away the top of the range, so that every
    // result is equally likely.
    const unsigned limit = 0xFFFFFFFFu - (0xFFFFFFFFu % n);
    unsigned v = 0;
    do { randomBytes(&v, sizeof(v)); } while (v >= limit);
    return v % n;
#else
    return arc4random_uniform(n);
#endif
}

std::string rmeConfigPath() { return supportDir() + "/rme.json"; }

bool loadRmeConfig(reasixty::rme::Config& out)
{
    FILE* f = std::fopen(rmeConfigPath().c_str(), "rb");
    if (!f) return false;
    std::string json;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) json.append(buf, n);
    std::fclose(f);
    if (json.empty()) return false;
    return reasixty::rme::configFromJson(json, out);
}

namespace {

// One after the other: the settings and the surface loop both save (their last
// write has to be the newest config, saveRmeConfigIfDirty).
std::mutex g_rmeSaveMx;

// ⛔ WHOLE FILES ONLY. Rea-Sixty reads rme.json about once a second, and writing
// into the file itself let a reader find it empty or cut off (test_orc_settings,
// two threads saving: 3656 of 8463 reads). A temporary file beside it, then
// rename(), which replaces in one step.
bool writeRmeJson(const reasixty::rme::Config& c)
{
    const std::string json = reasixty::rme::configToJson(c);
    const std::string path = rmeConfigPath();
    const std::string tmp = path + "." + std::to_string(orc_files::currentPid());
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(json.data(), 1, json.size(), f) == json.size();
    ok = std::fclose(f) == 0 && ok;
    if (ok) ok = orc_files::replaceFile(tmp, path);
    if (!ok) orc_files::removeFile(tmp);
    return ok;
}

} // namespace

bool saveRmeConfig(const reasixty::rme::Config& c)
{
    std::lock_guard<std::mutex> lk(g_rmeSaveMx);
    return writeRmeJson(c);
}

bool reaperWantsUf1()
{
    return orc_files::processAlive(reaperMarkerPid());
}

void writeOrcPid()
{
    orc_handover::writeFile(supportDir() + "/orc.pid", orc_handover::formatPid(orc_files::currentPid()));
}

void removeOrcPid()
{
    orc_files::removeFile(supportDir() + "/orc.pid");
}

int reaperMarkerPid()
{
    return orc_handover::parseMarker(orc_handover::readFile(supportDir() + "/handover")).pid;
}

int reaperMarkerVersion()
{
    return orc_handover::parseMarker(orc_handover::readFile(supportDir() + "/handover")).version;
}

void markReleased(int reaperPid, const std::string& positions, const std::string& modes)
{
    orc_handover::writeFile(supportDir() + "/handover.released",
                            orc_handover::formatReleased(reaperPid, positions, modes));
}

std::string takeReturned()
{
    const std::string path = supportDir() + "/handover.returned";
    std::string t = orc_handover::readFile(path);
    orc_files::removeFile(path);
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
    return t;
}

void clearReleased()
{
    orc_files::removeFile(supportDir() + "/handover.released");
}

std::string saveRmeConfigIfDirty()
{
    auto& mgr = reasixty::rme::manager();
    std::lock_guard<std::mutex> lk(g_rmeSaveMx);
    if (!mgr.takeConfigDirty()) return {};
    if (writeRmeJson(mgr.config())) return {};
    return "Cannot write " + rmeConfigPath();
}

std::string linkSummary()
{
    auto& mgr = reasixty::rme::manager();
    switch (mgr.link()) {
        case reasixty::rme::LinkState::Off:     return "off";
        case reasixty::rme::LinkState::Waiting: return "waiting for TotalMix";
        case reasixty::rme::LinkState::Online:  return "online";
        case reasixty::rme::LinkState::PortBusy:
        case reasixty::rme::LinkState::Silent:  return mgr.status();
    }
    return mgr.status();
}

} // namespace orc
