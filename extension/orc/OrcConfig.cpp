#include "OrcConfig.h"

#include "RmeManager.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <cerrno>
#include <csignal>
#include <sys/types.h>

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

bool saveRmeConfig(const reasixty::rme::Config& c)
{
    const std::string json = reasixty::rme::configToJson(c);
    const std::string path = rmeConfigPath();
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(json.data(), 1, json.size(), f) == json.size();
    std::fclose(f);
    return ok;
}

bool reaperWantsUf1()
{
    FILE* f = std::fopen((supportDir() + "/handover").c_str(), "rb");
    if (!f) return false;
    int pid = 0;
    const bool got = std::fscanf(f, "%d", &pid) == 1;
    std::fclose(f);
    if (!got || pid <= 0) return false;
    // kill(pid, 0) sends nothing and answers "does it exist": 0, or EPERM for a
    // process that exists but is not ours to signal.
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

std::string saveRmeConfigIfDirty()
{
    auto& mgr = reasixty::rme::manager();
    if (!mgr.takeConfigDirty()) return {};
    if (saveRmeConfig(mgr.config())) return {};
    return "Cannot write " + rmeConfigPath();
}

} // namespace orc
