#pragma once
//
// ORC's Hue, OBS and DynaMount settings (docs/orc-uf8-plan.md, section 6): one
// file per value in <supportDir>/modes, named like Rea-Sixty's ExtState keys and
// in the form each manager writes (ModeSettings.h). ORC owns them; Import copies
// Rea-Sixty's over on request, never on its own.

#include <string>

namespace orc::modes {

std::string dir();

// Read the files into the managers (modesettings::apply) and start OBS.
void load();

// One file. Writes are whole-file; a value never holds a newline. ⇨ A value
// that changes gets a time stamp beside it (<key>.stamp, ModeSync.h), now, or
// the one it came with from Rea-Sixty.
void write(const char* key, const std::string& value);
void write(const char* key, const std::string& value, long long stamp);
std::string read(const char* key);
long long stamp(const char* key);

// ⇨ IN STEP WITH REA-SIXTY AT THE HANDOVER (ModeSync.h). syncOut: ORC's values
// with their stamps, for handover.released (saves what is pending first).
// syncIn: Rea-Sixty's from handover.returned, every one that is newer than
// ORC's taken and applied; returns how many.
std::string syncOut();
int syncIn(const std::string& returned);

// Called every pass of the surface loop: writes what a manager marked changed
// (Hue and OBS config and keys, DynaMount presets) and the stands' positions
// when they moved. Cheap when nothing did.
void saveIfDirty();

// Copy Rea-Sixty's values out of REAPER's reaper-extstate.ini (section
// rea_sixty). Returns how many came over; 0 when REAPER or the values are not
// there. Applies them at once.
int importFromReaper();

// While REAPER holds the surfaces ORC's workers stay off: one program talks to
// the bridge, OBS and the stands at a time (the handover, section 5).
void stopWorkers();
void startWorkers();

} // namespace orc::modes
