#pragma once
//
// The six rme_* builtins: the TotalMix control room and the side-car's own
// keys, as actions a soft key or any other key can be bound to.
//
// ⇨ MOVED OUT OF main.cpp ON 2026-09-25 so ORC registers the same six, with the
// same labels and the same lamps. Before that ORC loaded the bindings engine
// and its factory bank named rme_dim and friends, but nothing had registered
// them there, so a press did nothing.
//
// tools/check_builtin_docs.py reads this file as well as main.cpp; a builtin
// registered here and missing from Bindings.cpp's doc table fails CI.

#include "RmeInput.h"

namespace reasixty::rme {

// Registers rme_dim, rme_mono, rme_speaker_b, rme_talkback, rme_show_window
// and rme_fader_main against `in`, the surface's side-car state, which must
// outlive the registration (both hosts keep it for the life of the program).
void registerBuiltins(input::State& in, const input::Host& host);

} // namespace reasixty::rme
