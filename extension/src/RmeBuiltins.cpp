#include "RmeBuiltins.h"

#include "Bindings.h"
#include "RmeManager.h"

namespace reasixty::rme {

void registerBuiltins(input::State& in, const input::Host& host)
{
    using uf8::bindings::registerBuiltin;
    using DescBuilder = uf8::bindings::BuiltinDescriptor;

    // ── TotalMix' control room (RME side-car factory bank, any surface) ──────
    // The same thread rule as OBS: the handler only queues an OSC message, the
    // lamp reads what TotalMix holds. TotalMix does not echo our own writes, so
    // the manager folds them in at once; the lamp is right the next tick.
    {
        using CR = Manager::ControlRoom;
        auto regRmeCr = [](const char* name, const char* addr, bool CR::* flag,
                           const char* label) {
            registerBuiltin(name, DescBuilder{
                [addr, flag](bool firing, bool /*pressed*/, int /*param*/) {
                    if (!firing) return;
                    auto& rm = manager();
                    rm.send(addr, (rm.controlRoom().*flag) ? 0.0f : 1.0f);
                },
                [flag](int) { return manager().controlRoom().*flag; },
                label, false
            });
        };
        regRmeCr("rme_dim",       "/controlroom/dim",      &CR::dim,      "RME: Dim main output");
        regRmeCr("rme_mono",      "/controlroom/mainmono", &CR::mono,     "RME: Main output mono");
        regRmeCr("rme_speaker_b", "/controlroom/speakerb", &CR::speakerB, "RME: Speaker B");
        regRmeCr("rme_talkback",  "/controlroom/talkback", &CR::talkback, "RME: Talkback");
    }
    // Das TotalMix-Fenster. ⚠ `/showwindow` steht in RMEs Tabelle (2.1 beta 2);
    // Franks TotalMix ist 2.10 alpha 8, wo `/status/*` und `/sendstate` fehlten.
    // Kommt das Fenster nicht, kennt diese Version die Adresse nicht. Die Lampe
    // zeigt, was wir zuletzt gesendet haben, denn TotalMix meldet nichts zurueck.
    registerBuiltin("rme_show_window", DescBuilder{
        [&in, &host](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            input::Writes w;
            input::toggleWindow(in, host, w);
            auto& rm = manager();
            for (const auto& [a, v] : w) rm.send(a, v);
        },
        [&in](int) { return in.windowShown.load(); },
        "RME: Show the TotalMix window", false
    });
    // Main auf den Fader des RME-Side-Cars (Frank 21.09.: der V-Pot-Druck waehlt
    // jetzt den Submix, also braucht Main einen eigenen Weg). Lampe an, solange
    // Main auf dem Fader liegt.
    registerBuiltin("rme_fader_main", DescBuilder{
        [&in](bool firing, bool /*pressed*/, int /*param*/) {
            if (firing) input::faderMainFire(in, manager().snapshot());
        },
        [&in](int) { return input::faderMainActive(in, manager().snapshot()); },
        "RME: Main on the fader", false
    });
}

} // namespace reasixty::rme
