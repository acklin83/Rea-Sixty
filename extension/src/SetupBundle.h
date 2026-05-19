// Setup-bundle save / restore — one file captures the user's whole
// Rea-Sixty configuration (bindings, user plug-in maps, parameter
// group slot meta, and every Settings preference held in ExtState).
//
// The bundle is intended for two use-cases:
//   1. User-side save/recall: share a setup between machines, snapshot
//      before experimenting, etc.
//   2. Factory-ship: Frank exports his canonical setup; we drop it
//      into the repo as default_setup.rea60config and Settings →
//      About's "Reset to factory" loads it back.
//
// Bundle format: JSON, version 1. The three sub-config JSONs are
// embedded as escaped string fields. ExtState values are stored as a
// {section: {key: value}} map. See exportToFile() for the canonical
// layout.
//
// Project-scoped state (per-RPP Selection Sets, per-track Parameter
// Group membership bitmasks) is NOT in the bundle — that lives in the
// .RPP and travels with the project.

#pragma once

#include <string>

namespace uf8 {
namespace setup_bundle {

// Write the current configuration to `path` as a .rea60config bundle.
// Returns true on success; on failure fills *errOut with a short
// reason if non-null.
bool exportToFile(const std::string& path, std::string* errOut);

// Read a bundle from `path` and apply it: overwrite the three on-disk
// config files, set every ExtState key it carries, then re-run each
// module's load() and the global ExtState reload helper so atomics
// and devices pick up the new state without a REAPER restart.
//
// Returns false if the bundle won't parse or its format identifier
// doesn't match. Partial-apply errors (one of the inner JSONs failed
// to persist) leave the in-memory state consistent and surface a
// warning via *errOut.
bool importFromFile(const std::string& path, std::string* errOut);

} // namespace setup_bundle
} // namespace uf8
