#include "ModeBuiltins.h"

#include "Bindings.h"
#include "DynaMountManager.h"
#include "HueManager.h"
#include "ObsManager.h"

#include <string>

namespace reasixty::modes {

using uf8::bindings::BuiltinDescriptor;
using uf8::bindings::registerBuiltin;

void registerBuiltins(const BuiltinHost& host)
{
    registerBuiltin("dynamount_preset_recall", BuiltinDescriptor{
        [host](bool firing, bool /*pressed*/, int param) {
            if (!firing) return;
            uf8::dynamount::manager().recallPreset(param > 0 ? param - 1 : 0);
            if (host.changed) host.changed();
        },
        [](int param) {
            return uf8::dynamount::manager().lastPreset() == (param > 0 ? param - 1 : 0);
        },
        "DynaMount: go to preset", true
    });
    registerBuiltin("dynamount_preset_store", BuiltinDescriptor{
        [host](bool firing, bool /*pressed*/, int param) {
            if (!firing) return;
            uf8::dynamount::manager().storePreset(param > 0 ? param - 1 : 0);
            if (host.changed) host.changed();
        },
        nullptr, "DynaMount: store preset", true
    });

    // Scene recall, parameter 1..8 = the slot in Settings → Modes → Hue. A
    // plain builtin rather than a dynamic bank, so a scene goes on any key of
    // any surface the bindings already reach. Long press starts the scene
    // dynamically (dynamic_palette) instead of just recalling it.
    registerBuiltin("hue_scene_recall", BuiltinDescriptor{
        [](bool firing, bool /*pressed*/, int param) {
            if (!firing) return;
            const int slot = (param > 0) ? (param - 1) : 0;
            uf8::hue::manager().recallSceneSlot(slot, /*dynamic=*/false);
        },
        nullptr, "Hue: recall scene", true
    });
    registerBuiltin("hue_scene_recall_dynamic", BuiltinDescriptor{
        [](bool firing, bool /*pressed*/, int param) {
            if (!firing) return;
            const int slot = (param > 0) ? (param - 1) : 0;
            uf8::hue::manager().recallSceneSlot(slot, /*dynamic=*/true);
        },
        nullptr, "Hue: start scene dynamically", true
    });
    registerBuiltin("hue_all_off", BuiltinDescriptor{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (firing) uf8::hue::manager().allOff();
        },
        nullptr, "Hue: all lamps off", false
    });
    if (host.recLight) {
        // Arms the recording light without touching the Settings pane, and reads
        // back its state so a bound key can light up while it is held.
        registerBuiltin("hue_rec_light", BuiltinDescriptor{
            [](bool firing, bool /*pressed*/, int /*param*/) {
                if (!firing) return;
                uf8::hue::RecLightConfig rc = uf8::hue::manager().recLight();
                rc.enabled = !rc.enabled;
                // No write here: setRecLight marks the config dirty and
                // tickHueTransport_ flushes it, which is the one place that saves.
                uf8::hue::manager().setRecLight(rc);
            },
            [](int) { return uf8::hue::manager().recLight().enabled; },
            "Hue: recording light on / off", false
        });
    }

    // ---- OBS ---------------------------------------------------------------
    // Everything here only POSTS: the manager's queue is drained by its own
    // worker, so a key press never waits for a socket. The LED reads the state
    // OBS reports, not the state we asked for — pressing record while OBS is
    // busy leaves the lamp dark, which is the honest answer.
    registerBuiltin("obs_record_toggle", BuiltinDescriptor{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            reasixty::obs::manager().toggleRecord();
        },
        [](int) { return reasixty::obs::manager().recording(); },
        "OBS: start / stop recording", false
    });
    registerBuiltin("obs_record_pause", BuiltinDescriptor{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            reasixty::obs::manager().toggleRecordPause();
        },
        [](int) { return reasixty::obs::manager().paused(); },
        "OBS: pause / resume recording", false
    });
    registerBuiltin("obs_chapter_marker", BuiltinDescriptor{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            // No name: OBS numbers them itself, and the input thread has no
            // business asking REAPER what the current marker is called.
            reasixty::obs::manager().chapter(std::string());
        },
        nullptr, "OBS: chapter mark in the recording", false
    });
    // ⇨ A NAME BEATS A NUMBER, and both are kept. The number counts positions
    // in OBS's scene list, and a position moves the moment the list is
    // reordered in OBS — every key bound to it then fires the wrong scene
    // without a word. A name survives that, so the step's text field wins when
    // it is filled and the number stays for the bindings that already use it.
    // The LED asks the same question the press does, so the two can never
    // disagree about which scene the key means.
    {
        auto obsSceneName = [](const uf8::bindings::ActionStep& st) {
            if (!st.textParam.empty()) return st.textParam;
            return reasixty::obs::manager().sceneAt(st.param - 1);  // 1-based
        };
        BuiltinDescriptor d{
            [](bool /*firing*/, bool /*pressed*/, int /*param*/) {},  // see runWithStep
            nullptr,                                                  // see stateOfStep
            "OBS: switch to scene", true
        };
        d.runWithStep = [obsSceneName](bool firing, bool /*pressed*/,
                                       const uf8::bindings::ActionStep& st) {
            if (!firing) return;
            const std::string nm = obsSceneName(st);
            if (!nm.empty()) reasixty::obs::manager().switchScene(nm);
        };
        d.usesText = true;
        d.stateOfStep = [obsSceneName](const uf8::bindings::ActionStep& st) {
            const std::string nm = obsSceneName(st);
            return !nm.empty() && nm == reasixty::obs::manager().currentScene();
        };
        registerBuiltin("obs_scene_recall", std::move(d));
    }

}

} // namespace reasixty::modes
