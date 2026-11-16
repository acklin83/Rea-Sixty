# Session 16.11.2026: ORC 1.7.0, eigene UF1-Jog-Modi

## ORC
- Medientasten-Build per Developer ID nach /Applications, dann ORC 1.7.0 (Build 15) released:
  https://github.com/acklin83/ORC/releases/tag/v1.7.0, /Applications aus dem DMG, Website-Knopf
  deployt von Frank.
- Franks Fireface UFX+ hat unter macOS keine Systemlautstärke (Core-Audio-Abfrage), Handbuchsatz
  `5bc65110`.

## Eigene Jog-Modi (`bf3978ba`, Plan docs/uf1-user-jog-modes-plan.md)
- `Uf1JogMode::User1..User4`, versteckt ab Werk. Pro Platz Name (8), Art (Links/Rechts oder eine
  Aktion mit Richtung), Teiler. Bindungen `Uf1JogUserNLeft/Right`, in bindings.json v58
  (`uf1_jog_user`). Kreuz-Blöcke auf 11, v58 füllt sie aus den physischen Tasten.
- Art B: REAPER-Action über `KBD_OnMainActionEx` relmode 1 (Kodierung wie REAPERs OSC-Fläche,
  Test gegen deren Funktion), Built-in mit der Raste als param. Leerer SHIFT-Platz = Plain feiner.
- Fader-Ziel pro Modus für alle Modi: Track, Item volume, Take volume, Last touched parameter.
  Ziel und Parameter beim Greifen festgehalten (behebt Sprung beim Moduswechsel unter der Hand),
  ein Undo-Schritt pro Griff für Item/Take. ExtState `uf1JogFaderT<m>`, alt `uf1JogFader<m>`=1 lädt
  als Item volume.
- Jog Mode: cycle läuft jetzt über die sichtbaren Modi in Ring-Reihenfolge.
- Panel-Tabelle `kJog`, REAPER-Actions „UF1 Jog Mode = User 1-4“, Built-ins `jog_mode_user1-4`.
- Test `test_uf1_jog_user`, `test_sidecar_keys` auf Version 58. ctest 48/48 lokal.

## Offen
- Nicht am Gerät geprüft (Frank testet). Tempo pro relativer REAPER-Action nur in REAPER zu sehen.
- Nicht released.
