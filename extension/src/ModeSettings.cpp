#include "ModeSettings.h"

#include "DynaMountManager.h"
#include "HueManager.h"
#include "ObsManager.h"

#include <cstdio>
#include <cstring>

namespace modesettings {

void apply(const Get& get)
{
    // DynaMount, as main.cpp's start loaded it until 29.10.2026.
    {
        uf8::dynamount::DynaMountManager& dm = uf8::dynamount::manager();
        if (const std::string blob = get("dynamount_devices"); !blob.empty())
            dm.deserialize(blob);
        const std::string fill = get("dynamount_fill");
        dm.setFillDir((!fill.empty() && fill[0] == 'R') ? uf8::dynamount::FillDir::Right
                                                        : uf8::dynamount::FillDir::Left);
        if (const std::string pr = get("dynamount_presets"); !pr.empty())
            dm.deserializePresets(pr);
        dm.takePresetsDirty();   // loading is not an edit
        // The last-known positions. markDirty=false: the stands physically hold
        // this pose already, so they are not driven on launch. "idx,h,r,v;".
        if (const std::string st = get("dynamount_state"); !st.empty()) {
            const char* p = st.c_str();
            while (*p) {
                int idx = 0, h = 0, r = 90, v = 0;
                if (std::sscanf(p, "%d,%d,%d,%d", &idx, &h, &r, &v) == 4)
                    dm.setTargets(idx, h, r, v, /*markDirty=*/false);
                const char* semi = std::strchr(p, ';');
                if (!semi) break;
                p = semi + 1;
            }
        }
        // Probe every enabled stand once so the surface and Settings show
        // online / offline without a Detect click each (Frank 2026-06-28).
        for (int i = 0; i < uf8::dynamount::kMaxMounts; ++i)
            if (dm.mountEnabled(i)) dm.requestDetect(i);
    }
    // Hue: the slots, then the application key (kept apart so a shared setup
    // can carry the slots without the credentials).
    {
        uf8::hue::HueManager& hm = uf8::hue::manager();
        if (const std::string blob = get("hue_config"); !blob.empty()) hm.deserialize(blob);
        if (const std::string key = get("hue_key"); !key.empty()) hm.deserializeCredentials(key);
    }
    // OBS: host and port, then the password, the same split.
    {
        auto& om = reasixty::obs::manager();
        if (const std::string blob = get("obs_config"); !blob.empty()) om.deserialize(blob);
        if (const std::string key = get("obs_key"); !key.empty()) om.deserializeCredentials(key);
    }
}

std::map<std::string, std::string> parseIni(const std::string& text, const std::string& section)
{
    std::map<std::string, std::string> out;
    bool in = false;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = eol + 1;
        if (!line.empty() && line.front() == '[') {
            const auto close = line.find(']');
            in = close != std::string::npos && line.substr(1, close - 1) == section;
            continue;
        }
        if (!in) continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        out[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return out;
}

} // namespace modesettings
