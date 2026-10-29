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

// The TotalMix link in a few words, for the menu bar and the settings page:
// "online", "off", "waiting for TotalMix", or the reason it is down. ⇨ Says
// nothing the port fields beside it already say (Frank 27.09.2026: "nicht
// wiederholen").
std::string linkSummary();

// ⇨ THE HANDOVER TO REA-SIXTY (Frank 25.09.2026, "Weg c"). Rea-Sixty, when its
// setting "Take the UF1 over from ORC" is on, writes its REAPER pid into
// <supportDir>/handover while it wants the UF1, and removes it on quit. True
// while that file names a process that is alive: ORC must let go and stay off.
// A file left behind by a crashed REAPER names a dead pid and is ignored.
bool reaperWantsUf1();

// ⇨ HANDOVER, SECOND HALF: THE RECEIPT (Frank 29.10.2026, docs/orc-uf8-plan.md
// section 5). Without it Rea-Sixty could only guess when ORC had let go and
// retried every 5 s. Now:
//   · <supportDir>/orc.pid holds ORC's pid while ORC runs. Rea-Sixty waits for
//     the receipt only when that process lives; no ORC, no wait.
//   · <supportDir>/handover.released holds the pid of the REAPER that asked,
//     written once ORC has closed the surfaces and its TotalMix port. Removed
//     when ORC takes them back.
// A marker from a Rea-Sixty that knows the receipt carries a second line "v2";
// older ones carry only the pid (reaperMarkerVersion).
void writeOrcPid();
void removeOrcPid();
// The pid in the handover marker, or 0 when there is none or it is unreadable.
int reaperMarkerPid();
// 2 for a marker with the "v2" line, 1 for a pid-only marker, 0 for none.
int reaperMarkerVersion();
void markReleased(int reaperPid);
void clearReleased();

} // namespace orc
