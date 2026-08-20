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

// One row of a browser. `name` is what a surface shows ("Adrian Hall/Kick In"
// for a preset inside a group), `path` the file to load.
struct Entry { std::string name, path; };

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

}  // namespace sslpreset
