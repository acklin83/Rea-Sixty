# Session 26./27.10.2026: MIDI-CC-Layer, v0.7.0, Plan für zwei UF8

## Gebaut
- UC1-Input-Meter liest auf Fixed-Lane-Spuren nur spielende Lanes (`aedc49ad`).
- UF1-Kleindisplay: Comp-GR folgt „GR meter source“ (`072a83c6`).
- Kupplung „Bank follows REAPER selection“ (`0443dc08`): nur Auswahl in REAPER gekuppelt, Hardware bankt immer.
- MIDI-CC-Layer auf dem UF8, bidirektional (`docs/midi-cc-layer-plan.md`): Sonde (Mac gemessen, Windows nur virtuelle Ports), Kern `MidiCcLayer.h` + ctests, Datenmodell in `bindings.json` (additiv, v55), Eingang/Ausgang/Anzeige, Settings (`SettingsScreen_MidiCc.cpp`), Rückweg aus Items und von Hardware, 6 Seiten (V-POT + 1..5), Handbuchkapitel.
- Handbuch: UF1-Soft-Key-LEDs als BC-GR-Meter, „Scroll banks“, MIDI CC layer.
- **Release v0.7.0 „Easy MIDI Rider“** (`ee3ac600`), alle Plattformen, ReaPack, Website.

## Entscheidungen
- MIDI-CC: Output pro Layer, Rückweg gewählte Spur + Hardware-Input, nur UF8, ganzer Layer, 14 bit pro Fader, Presets; Abschnitt 6 des Plans nach Vorschlag.
- PR #10 (Tim Shadgett, zwei UF8): gemischter Weg, Plan `docs/dual-uf8-plan.md` mit allen Entscheiden; Antwort an Tim gepostet.

## Fallen
- Bindings-Settings-Pick-Gate schluckt CC-Tasten als „nur auswählen“: CC-Block steht davor.
- WDL definiert unter Windows `snprintf` als Makro: in neuen Dateien plain `snprintf`.

## Offen
- Zwei UF8: Stufe 1 (Tims Geräteteil per Cherry-Pick, Einstellung „Second UF8“ Werk aus, Swap als Builtin).
