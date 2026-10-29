#include "OrcModes.h"

#include "DynaMountManager.h"
#include "HueManager.h"
#include "ModeSettings.h"
#include "ModeSync.h"
#include "ObsManager.h"
#include "OrcConfig.h"
#include "OrcHandover.h"
#include "OrcLog.h"

#include <cstdlib>
#include <sys/stat.h>

namespace orc::modes {

std::string dir()
{
    const std::string d = orc::supportDir() + "/modes";
    ::mkdir(d.c_str(), 0755);
    return d;
}

std::string read(const char* key)
{
    std::string v = orc_handover::readFile(dir() + "/" + key);
    while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
    return v;
}

long long stamp(const char* key)
{
    return std::atoll(orc_handover::readFile(dir() + "/" + key + ".stamp").c_str());
}

void write(const char* key, const std::string& value, long long st)
{
    // A newline would cut the value when Rea-Sixty imports it into ExtState.
    if (value.find('\n') != std::string::npos) return;
    if (value == read(key)) return;   // unchanged: no write, no new stamp
    orc_handover::writeFile(dir() + "/" + key, value);
    orc_handover::writeFile(dir() + "/" + key + ".stamp", std::to_string(st));
}

void write(const char* key, const std::string& value)
{
    write(key, value, modesync::nowMs());
}

void load()
{
    modesettings::apply([](const char* key) { return read(key); });
    reasixty::obs::manager().start();
}

void saveIfDirty()
{
    auto& hm = uf8::hue::manager();
    if (hm.takeConfigDirty()) write("hue_config", hm.serialize());
    auto& om = reasixty::obs::manager();
    if (om.takeConfigDirty()) write("obs_config", om.serialize());
    // The keys: pairing and the password field change them without a config
    // edit, so they are compared rather than flagged.
    static std::string lastHueKey = read("hue_key"), lastObsKey = read("obs_key");
    if (const std::string k = hm.serializeCredentials(); k != lastHueKey) {
        lastHueKey = k;
        write("hue_key", k);
    }
    if (const std::string k = om.serializeCredentials(); k != lastObsKey) {
        lastObsKey = k;
        write("obs_key", k);
    }
    auto& dm = uf8::dynamount::manager();
    if (dm.takePresetsDirty()) write("dynamount_presets", dm.serializePresets());
    // The positions, the way Rea-Sixty keeps them (persistDynaState_): row-keyed
    // for ORC's own next start.
    static std::string lastState;
    std::string st;
    char buf[48];
    for (int i = 0; i < uf8::dynamount::kMaxMounts; ++i) {
        if (!dm.mountEnabled(i)) continue;
        snprintf(buf, sizeof(buf), "%d,%d,%d,%d;", i, dm.targetH(i), dm.targetR(i), dm.targetV(i));
        st += buf;
    }
    if (st != lastState) {
        lastState = st;
        write("dynamount_state", st);
    }
}

int importFromReaper()
{
    const char* home = std::getenv("HOME");
    if (!home) return 0;
    const std::string ini = orc_handover::readFile(
        std::string(home) + "/Library/Application Support/REAPER/reaper-extstate.ini");
    if (ini.empty()) return 0;
    const auto vals = modesettings::parseIni(ini, "rea_sixty");
    int n = 0;
    for (const char* key : modesettings::kImportKeys) {
        const auto it = vals.find(key);
        if (it == vals.end() || it->second.empty()) continue;
        write(key, it->second);
        ++n;
    }
    if (n > 0) modesettings::apply([](const char* key) { return read(key); });
    orc::logLine("ORC: imported %d mode setting(s) from REAPER\n", n);
    return n;
}

std::string syncOut()
{
    saveIfDirty();
    return modesync::format(modesync::collect([](const char* k) { return read(k); },
                                              [](const char* k) { return stamp(k); }));
}

int syncIn(const std::string& returned)
{
    // ORC's own edits made while REAPER held the surfaces are stamped first, so
    // they win where they are the newer.
    saveIfDirty();
    int n = 0;
    for (const auto& v : modesync::parse(returned)) {
        bool known = false;
        for (const char* k : modesettings::kImportKeys) known = known || v.key == k;
        if (!known) continue;
        if (!modesync::takeIncoming(stamp(v.key.c_str()), v.stamp, v.value, /*incomingIsOrc*/ false))
            continue;
        write(v.key.c_str(), v.value, v.stamp);
        ++n;
    }
    if (n > 0) modesettings::apply([](const char* key) { return read(key); });
    orc::logLine("ORC: took %d Hue/DynaMount/OBS value(s) from REAPER\n", n);
    return n;
}

void stopWorkers()
{
    uf8::hue::manager().stop();
    reasixty::obs::manager().stop();
    uf8::dynamount::manager().stop();
}

void startWorkers()
{
    uf8::hue::manager().start();
    reasixty::obs::manager().start();
    uf8::dynamount::manager().start();
}

} // namespace orc::modes
