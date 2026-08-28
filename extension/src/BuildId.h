#pragma once
//
// BuildId — which binary is actually running.
//
// Its own translation unit for two reasons. `commit_count.h` changes on every
// commit and only its OBJECT_DEPENDS owner recompiles for it, so putting this
// in a header that main.cpp includes would rebuild the largest file in the
// project every time. And the impersonator is linked into test binaries that
// have no Settings screen, so the definition cannot live there either.

namespace uf8 {

// `git describe` as of configure time, e.g. "v0.5.8-14-g4dd67f5-dirty".
const char* reasixtyBuildId();

}  // namespace uf8
