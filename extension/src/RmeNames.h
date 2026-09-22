#pragma once
//
// RmeNames — the names of TotalMix' eight snapshots and eight layouts.
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
// Windows and Linux: no path known, so no names; the banks fall back to
// "Snapshot n" / "Layout n" rather than guessing a folder.
//
#include <string>

namespace reasixty::rme {

struct Names {
    std::string snapshot[8];   // "" = TotalMix has no name for it
    std::string layout[8];
};

// Pure: reads SnapshotName 0-7 / LayoutName 0-7 out of the file's text. Returns
// false when the text holds neither (not a TotalMix state file).
bool parseNames(const std::string& xml, Names& out);

// The display name of slot `i` (0..7), with the fallback applied.
std::string snapshotName(const Names& n, int i);
std::string layoutName(const Names& n, int i);

// The newest last.*.xml in TotalMix' folder, re-read only when its path or
// modification time changed. Cheap to call often: the check itself is
// rate-limited to once every two seconds. Main thread.
const Names& namesFromDisk();

}  // namespace reasixty::rme
