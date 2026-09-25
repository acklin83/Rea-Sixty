#pragma once
//
// ORC's own files, and where they live.
//
// ⛔ ONE FILE, ONE WRITER, AND THE WRITER IS ORC. Frank, 25.09.2026 evening:
// "Die ganze Konfig für den RME Side-Car kommt in ORC. Rea-Sixty übernimmt dann
// die dortigen Einstellungen." Rea-Sixty's side-car reads this rme.json and
// never writes it; its own copy and its RME settings page are gone. (Earlier
// the same day each program had its own file; the rule that survived is the
// one about writers.)
//
//   ~/Library/Application Support/ORC/orc.json   the bindings (stage 6)
//   ~/Library/Application Support/ORC/rme.json   the TotalMix link
//
// The format is the extension's, byte for byte: configToJson / configFromJson
// out of RmeManager. One reader, one writer, one shape.

#include <string>

namespace reasixty::rme { struct Config; }

namespace orc {

// ~/Library/Application Support/ORC, created if it is not there. Falls back to
// the working directory when HOME is unset, which is what the bindings host
// does too.
std::string supportDir();

std::string rmeConfigPath();

// Reads rme.json into `out`. False when there is no file yet or it does not
// parse; `out` is left as the caller had it, so a broken file keeps the
// defaults rather than emptying the roles.
bool loadRmeConfig(reasixty::rme::Config& out);

// Writes rme.json. Returns false if the file cannot be written, which the
// window shows rather than swallowing.
bool saveRmeConfig(const reasixty::rme::Config& c);

// Writes rme.json only when a setter actually ran, the rule the extension's
// tick uses as well (Manager::takeConfigDirty). Returns an empty string when
// there was nothing to do or the write went through, and the reason otherwise.
// ⇨ Lives here rather than in the window: what is saved and when should not
// depend on whether anybody is looking at a settings page.
std::string saveRmeConfigIfDirty();

// ⇨ THE HANDOVER TO REA-SIXTY (Frank 25.09.2026, "Weg c"). Rea-Sixty, when its
// setting "Take the UF1 over from ORC" is on, writes its REAPER pid into
// <supportDir>/handover while it wants the UF1, and removes it on quit. True
// while that file names a process that is alive: ORC must let go and stay off.
// A file left behind by a crashed REAPER names a dead pid and is ignored.
bool reaperWantsUf1();

} // namespace orc
