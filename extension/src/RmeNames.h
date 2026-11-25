#pragma once
//
// RmeNames — the names of TotalMix' snapshots and layouts. The file has eight of
// each, TotalMix offers six layouts (kLayoutSlots in RmeState.h).
//
// ⛔ OSC DOES NOT CARRY THEM. /snapshot/load/<n> answers a state (0 / 2 / 3) and
// /layout/load/<n> answers nothing at all. The names live only in TotalMix'
// own state file, ~/Library/Application Support/RME TotalMix FX/last.<device>.xml
// (Frank's: last.FirefaceUFX+23802132.xml, lines 70-86, read 2026-09-22):
//
//     <val e="SnapshotName 3" v="Uncr\xF6wned"/>
//     <val e="LayoutName 3"   v="Tracking"/>
//
// Two things measured in that file and pinned in tests/test_rme_osc.cpp:
//  · THE FILE IS LATIN-1, not UTF-8: the "ö" above is the single byte F6. The
//    rest of Rea-Sixty speaks UTF-8, so the parser converts (and leaves a value
//    alone that already is valid UTF-8, in case a later TotalMix changes that).
//  · WHEN TotalMix rewrites the file is not known. A rename in TotalMix shows up
//    here once the file changes on disk, not the moment it is typed.
//
// Windows: %LOCALAPPDATA%\TotalMixFX (orc_files::totalMixFolder, read on the
// StoerPC). Linux: no TotalMix, so no names; the banks fall back to
// "Snapshot n" / "Layout n" rather than guessing a folder.
//
#include <string>
#include <vector>

namespace reasixty::rme {

struct Names {
    std::string snapshot[8];   // "" = TotalMix has no name for it
    std::string layout[8];
};

// Pure: reads SnapshotName 0-7 / LayoutName 0-7 out of the file's text. Returns
// false when the text holds neither (not a TotalMix state file).
bool parseNames(const std::string& xml, Names& out);

// `s` as UTF-8: unchanged when it already is valid UTF-8, otherwise read as
// Latin-1. The same rule parseNames applies to the state file, for any other
// text TotalMix hands over (ORC MoreMe sends channel names to a browser, which
// closes a WebSocket on a text frame that is not UTF-8).
std::string toUtf8(const std::string& s);

// The display name of slot `i` (0..7), with the fallback applied.
std::string snapshotName(const Names& n, int i);
std::string layoutName(const Names& n, int i);

// ⇨ WHICH last.*.xml (Frank 25.11.2026, forum: a second UFX III after a warranty
// swap left two files, TotalMix kept writing both, and the newest was the old
// unit's, so ORC showed "Layout 1" and "Mix 1"). TotalMix names the device its
// window shows in rme.totalmix.default.xml, <val e="DeviceName" v="Fireface UFX+
// (23802132)"/>, and that name without spaces and brackets is the file's:
// last.FirefaceUFX+23802132.xml (Frank's Mac, read 25.11.2026). The newest file
// stays the fallback, for a folder without that entry or without that file.
// Pure, for the test:
//   stateFileKey  "Fireface UFX+ (23802132)" -> "FirefaceUFX+23802132"
//   shownDevice   the first DeviceName in the text of rme.totalmix.default.xml, or ""
//   pickStateFile the shown device's file when it is among `files`, else the newest
std::string stateFileKey(const std::string& deviceName);
std::string shownDevice(const std::string& defaultXml);
struct StateFile { std::string name; long long mtime = 0; };
std::string pickStateFile(const std::vector<StateFile>& files, const std::string& device);

// The state file of the device TotalMix shows (pickStateFile), re-read only when
// its path or modification time changed. Cheap to call often: the check itself
// is rate-limited to once every two seconds. Main thread.
const Names& namesFromDisk();

}  // namespace reasixty::rme
