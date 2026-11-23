#pragma once
//
// ⇨ ORC PROFILES (Frank 23.11.2026, from the RME forum: "is saving and moving that
// file sufficient to save or duplicate a particular ORC configuration"). One file,
// <name>.orcprofile, written by Export… and read by Import… on the General page.
//
// ⛔ WHAT A PROFILE CARRIES IS WHAT YOU PLAY, NOT THE COMPUTER (Frank 23.11.2026,
// answer A):
//   · orc.json                 the keys, the soft-key banks, their colours
//   · rme.json                 everything but the TotalMix connection
//   · moreme.json              studio name and logo, not the switch or the ports
//   · moreme-performers.json, the logo picture, DynaMount's presets and fill side
// It stays on the computer: the TotalMix connection (enabled, host, ports), MoreMe's
// switch, ports and PIN, the Hue and OBS keys and settings (bridge, address,
// password), the DynaMount stands' addresses and positions. So a profile holds no
// password, and one made on another computer does not cut this one's links.
//
// ⛔ AN IMPORT IS APPLIED AT THE NEXT START, BEFORE ANYTHING IS LOADED. A running
// ORC writes its files again as it closes (closeDown: modes::saveIfDirty, the
// link's config), so files written under it would be overwritten. Import… checks
// the file and puts a copy at <supportDir>/profile-import.orcprofile; ORC restarts;
// main() calls applyStaged() first thing. Before writing, the files as they are go
// to <supportDir>/profiles/before-import-<time>.orcprofile (Frank 23.11.2026).
//
// The format: JSON with "format": "orc-profile", "version": 1, the ORC version and
// the time it was made, and "files": { name: base64 }. Base64 because the files
// are not all UTF-8 (TotalMix names come in as Latin-1) and a logo is binary.

#include <string>
#include <vector>

namespace orc::profile {

constexpr const char* kExtension = "orcprofile";
constexpr int         kVersion   = 1;

struct File {
    std::string name;   // "orc.json", "modes/dynamount_presets", "logo/moreme-logo.png"
    std::string data;
};

struct Info {
    int         version = 0;
    std::string orc;        // the ORC that made it
    std::string created;    // "2026-11-23 14:05"
};

// ── pure ──────────────────────────────────────────────────────────────────────
std::string pack(const std::vector<File>& files, const Info& info);
// False with `err` for anything that is not a profile this ORC can read, or one
// without orc.json and rme.json.
bool unpack(const std::string& text, std::vector<File>& files, Info& info, std::string& err);
// rme.json from the profile, with this computer's connection (enabled, host, ports).
std::string mergeRme(const std::string& local, const std::string& fromProfile);
// moreme.json: studio and logo from the profile, the switch and the ports from here.
std::string mergeMoreMe(const std::string& local, const std::string& fromProfile);
std::string base64Decode(const std::string& in, bool& ok);

// ── ORC's folder ──────────────────────────────────────────────────────────────
// The profile of what is on disk now. Reads only: applyStaged takes it at start,
// before anything is loaded.
std::string current();
// saveRmeConfigIfDirty first, so the link's last edit is in the file.
bool exportTo(const std::string& path, std::string& err);
// Reads and checks `path`; `info` says what it is. Nothing is written.
bool check(const std::string& path, Info& info, std::string& err);
// Copies a checked profile to profile-import.orcprofile for the next start.
bool stage(const std::string& path, std::string& err);
// At start, before the files are read: the staged profile, if there is one, saved
// over the files, the old ones first to profiles/. Returns a line for the log and
// the settings page ("" when nothing was staged); lastApplied() keeps it.
std::string applyStaged();
const std::string& lastApplied();

} // namespace orc::profile
