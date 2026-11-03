#include "OrcConfig.h"

#include "RmeManager.h"
#include "OrcHandover.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <unistd.h>

namespace orc {

std::string supportDir()
{
    const char* home = std::getenv("HOME");
    if (!home) return ".";
    const std::string d = std::string(home) + "/Library/Application Support/ORC";
    // Best effort: an existing directory returns EEXIST, which is the outcome
    // we want. A failure here shows up as a failed write later, with a path.
    ::mkdir(d.c_str(), 0755);
    return d;
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
    const std::string tmp = path + "." + std::to_string(::getpid());
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(json.data(), 1, json.size(), f) == json.size();
    ok = std::fclose(f) == 0 && ok;
    if (ok) ok = std::rename(tmp.c_str(), path.c_str()) == 0;
    if (!ok) std::remove(tmp.c_str());
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
    const int pid = reaperMarkerPid();
    if (pid <= 0) return false;
    // kill(pid, 0) sends nothing and answers "does it exist": 0, or EPERM for a
    // process that exists but is not ours to signal.
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

void writeOrcPid()
{
    orc_handover::writeFile(supportDir() + "/orc.pid", orc_handover::formatPid(::getpid()));
}

void removeOrcPid()
{
    std::remove((supportDir() + "/orc.pid").c_str());
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
    std::remove(path.c_str());
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
    return t;
}

void clearReleased()
{
    std::remove((supportDir() + "/handover.released").c_str());
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
