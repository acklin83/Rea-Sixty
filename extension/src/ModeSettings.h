#pragma once
//
// The settings of the Hue, DynaMount and OBS modes as stored values, one per
// key, in the form each manager writes them (serialize / deserialize). Rea-Sixty
// keeps them in REAPER's ExtState (section rea_sixty), ORC in files named like
// the keys in <ORC>/modes. Each program owns its own; the Import buttons copy
// from the other (docs/orc-uf8-plan.md section 6: a shared folder broke on
// install order, version skew, Windows Store apps and portable REAPER).
// Test: tests/test_mode_settings.cpp.

#include <functional>
#include <map>
#include <string>

namespace modesettings {

// What an Import copies: the configuration, not the positions of the stands
// (dynamount_state), which only travel with the handover.
inline const char* const kImportKeys[] = {
    "hue_config", "hue_key", "obs_config", "obs_key",
    "dynamount_devices", "dynamount_fill", "dynamount_presets",
};

// Read a stored value by key; "" when there is none.
using Get = std::function<std::string(const char* key)>;

// Put stored values into the three managers, as Rea-Sixty's start always did:
// DynaMount (devices, fill, presets, positions without driving the stands, a
// detect per enabled stand), Hue (config, then the key), OBS (config, then the
// password). Starts no worker; Rea-Sixty and ORC start OBS's themselves.
void apply(const Get& get);

// The [section] of a REAPER ExtState file (reaper-extstate.ini): key=value
// lines until the next [section]. Values are taken as they are, up to the end
// of the line.
std::map<std::string, std::string> parseIni(const std::string& text, const std::string& section);

} // namespace modesettings
