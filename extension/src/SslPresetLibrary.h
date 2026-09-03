#pragma once
//
// SslPresetLibrary — SSL's own on-disk preset library, for every surface.
//
// SSL's plug-ins expose NO presets to the host: REAPER's TrackFX_GetPreset is
// empty for all of them. That is why the UC1's PRESETS screen had nothing to
// show and why the UF1's browser had to be built from scratch. The library
// itself is on disk, one folder per plug-in, and this reads it — so both
// surfaces list the same presets and load them the same way, instead of one
// growing a private copy of the other's answer.
//
// Loading goes through the plug-in's own state (PluginChunkPatch), which is what
// makes an instance SHOW the preset rather than "that preset, modified"; the
// host-parameter path stays as the fallback for a chunk that cannot be rewritten.
//
#include <string>
#include <vector>

class MediaTrack;

namespace sslpreset {

// One preset. `name` is the file's own name, `group` the folder path below the
// library root it sits in ("" at the top, "Producer Presets/Adrian Hall" deeper
// down, always '/'-separated whatever the platform), `path` the file to load.
// A surface that browses folders walks `group`; a flat one can show `display()`.
struct Entry {
    std::string name, group, path;
    // A preset REAPER knows about rather than one of SSL's files: `path` then
    // carries what TrackFX_SetPreset wants — the preset NAME for VST2 / AU /
    // user presets, the full .vstpreset path for VST3. Set by reaperListFor.
    bool host = false;
    // For a list with no folder navigation: the DEEPEST folder plus the name,
    // because a row is 14 to 24 bytes and the deepest folder is the part that
    // says something ("Adrian Hall/Kick In", not "Producer Presets/Adrian…").
    std::string display() const {
        if (group.empty()) return name;
        const size_t sl = group.find_last_of('/');
        return (sl == std::string::npos ? group : group.substr(sl + 1)) + "/" + name;
    }
};

// SSL's preset folder for this FX, or "" when the plug-in has none (or the
// platform has no SSL plug-ins at all).
std::string presetDir(MediaTrack* tr, int fx);

// Every preset under `dir`, recursively, naturally sorted.
std::vector<Entry> scan(const std::string& dir);

// presetDir + scan in one call — what a browser wants.
std::vector<Entry> listFor(MediaTrack* tr, int fx);

// Load one preset file into this instance. Returns the number of values
// written, 0 if nothing could be applied.
int load(MediaTrack* tr, int fx, const std::string& path);

// ---- REAPER's own library, for the plug-ins that are not SSL's --------------
//
// Everything above is SSL-only by construction: their plug-ins keep their
// presets on disk and show the host none. Every OTHER plug-in is the other way
// round — REAPER knows its presets and there is no SSL folder to read. REAPER
// keeps both lists as plain .ini next to each other: the user's own where
// TrackFX_GetUserPresetFilename points, the factory list beside it as
// "…-builtin.ini" ([factory] for VST2 program names, [aufactory] for AU,
// [vstpreset] for VST3, where the value is the .vstpreset path). Reading the
// names off disk keeps the browser's promise that BROWSING IS FREE: stepping
// presets through the host to learn their names would load every one of them.
std::vector<Entry> reaperListFor(MediaTrack* tr, int fx);

// SSL's library when the plug-in has one, REAPER's otherwise. What a browser
// wants: one call, and it never has to know which kind of plug-in it is on.
std::vector<Entry> listAnyFor(MediaTrack* tr, int fx);

// Load whichever kind the entry is. Returns >0 on success.
int loadEntry(MediaTrack* tr, int fx, const Entry& e);

// The preset the instance reports as loaded — SSL's own attribute for an SSL
// plug-in, TrackFX_GetPreset for everything else. "" when nothing is known,
// which is what a fresh instance looks like.
std::string currentNameFor(MediaTrack* tr, int fx);

}  // namespace sslpreset
