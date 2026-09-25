#pragma once
//
// ORC's face: the menu-bar item and the settings window. AppKit lives behind
// this header and nowhere else, so everything above it stays plain C++.
//
// ⛔ NOT A MIXER ON SCREEN. Frank, 25.09.2026: "wir brauchen ja eigentlich nur
// die settings, die ganze bedienung machen wir ja eh übers uf1." ORC's controls
// are the surface. What runs on the Mac is a status item that says who holds
// what, and a page for the things a knob cannot set: ports, which role sits on
// which pot, which colour becomes which.
//
// ⛔ AppKit, not Dear ImGui. The first stage-7 build drew its own window with
// ImGui and it looked like a tool, not like a Mac program. Also: vendor/ is in
// .gitignore (WDL and the REAPER SDK arrive by FetchContent), so a vendored
// ImGui would have lived on one machine and CI could never have built ORC.
//
// ⛔ Objective-C++, no Swift. A Swift target would be a second toolchain in a
// CMake build that has none; AppKit answers the same questions from .mm files
// that already compile here.

#include <functional>

namespace orc {

class Surface;

// Installs the status item and runs the macOS event loop ON THE CALLING THREAD,
// which has to be the main thread. Returns when the user quits, so main() can
// close the surface and the link in order.
//
// `interrupted` is polled a few times a second. ⛔ It exists because a signal
// handler may not call into AppKit: Ctrl-C only sets a flag, and the loop reads
// it from a timer, on the thread AppKit expects.
void runApp(Surface& surface, const std::function<bool()>& interrupted);

} // namespace orc
