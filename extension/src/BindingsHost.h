#pragma once
//
// BindingsHost — the four things the bindings engine cannot answer by itself.
//
// Bindings.cpp is 8904 lines and asks REAPER exactly five questions. Everything
// else about layers, modifiers, long press, step chains, dynamic banks and the
// config file is arithmetic over its own data. ORC drives the same surfaces from
// the same bindings and would otherwise need a second engine, which for a file
// this size and this subtle is not a copy anyone could keep straight.
//
// So the five questions become a host interface. Rea-Sixty answers them with
// REAPER; ORC answers the ones it has and leaves the rest empty. An unset
// callback is not an error: a host without MIDI simply sends none.
//
// ⛔ THIS IS THE ONLY PLACE Bindings.cpp IS ALLOWED TO LOOK OUTWARDS. If a sixth
// question turns up, it goes in here rather than back into an #include of
// reaper_plugin_functions.h, or the file stops being shareable and ORC grows its
// own copy after all.
//

#include <functional>
#include <string>

#include "KeyMacro.h"

namespace uf8 {
namespace bindings {

struct Host {
    // The directory the bindings file lives in, in full. Rea-Sixty appends
    // rea_sixty to REAPER's resource path; ORC names its own folder. The
    // "/rea_sixty" used to be added down inside the engine, which is a name only
    // one of the two hosts has any business carrying.
    std::function<std::string()> configDir;

    // The file's name inside that directory. Empty means bindings.json.
    std::function<std::string()> configFile;

    // A REAPER action name to its command id, 0 when there is no such action or
    // no REAPER at all. ORC returns 0 and the binding does nothing, which is the
    // honest outcome for an action that cannot exist without a DAW.
    std::function<int(const char*)> namedCommand;

    // Send one MIDI message. `device` empty means every enabled output. The
    // caller has already clamped the data bytes and decided the status.
    std::function<void(int status, int d1, int d2, const std::string& device)> sendMidi;

    // Type a key chord at the host application.
    std::function<void(const keymacro::KeyChord&)> sendKeyChord;
};

// The installed host. Never null; the default answers nothing.
const Host& host();
void setHost(Host h);

} // namespace bindings
} // namespace uf8
