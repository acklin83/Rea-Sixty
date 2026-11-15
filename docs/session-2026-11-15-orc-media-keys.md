# Session 15.11.2026: ORC media keys

Anlass: Forenpost (creal: UF1 für JRiver/Spotify ohne REAPER). Frank: nur Built-ins für ORC, keine Anzeige.
Plan und Entscheide: `docs/orc-media-keys-plan.md`.

## Gebaut
- `orc/OrcMedia.h`: sechs Built-ins (Kategorie Media), Jog-Ziel `system`.
- `orc/OrcMediaMac.mm`: Medientasten als systemdefinierte Ereignisse (Subtyp 8), Freigabe-Anfrage
  einmal pro Lauf; Systemlautstärke über Core Audio.
- `orc/OrcMediaWin.cpp`: `SendInput`, `IAudioEndpointVolume` (ole32 gelinkt).
- `orc/OrcActions.h`: `offersAction` (Seite + Handbuch-Dump, vorher zweimal abgeschrieben),
  `runsSlot` (UF8-Tasten und jetzt auch UF1-Transportlampe).
- `orc/Surface.cpp`: Registrierung, Werksbelegung PLAY/FFW/RWD, Jog auf Systemlautstärke.
- `orc/Uf8Surface.cpp`: CHANNEL-Encoder folgt dem Jog-Ziel.
- Einstellungen: Jog-Menü mit „System volume“ (`jogTargets`).
- `src/Bindings.cpp`: Beschreibungen, Labels, Kategorie Media. `check_builtin_docs.py` liest `orc/OrcMedia.h`.
- Handbuch: Transporttasten, Jog, CHANNEL, Abschnitt „Media keys“, ORC und Rea-Sixty.
- Test `testMediaKeys` in `test_orc_settings` (auch: `orcKeySlot` bleibt null für media_).

## Nicht geprüft
- Am Gerät nichts (Frank testet). Ob macOS die Ereignisse ohne Freigabe verwirft, ob die
  Freigabe-Frage erscheint, wie JRiver reagiert.
- Ob ein RME-Gerät unter macOS eine Systemlautstärke anbietet.
- Windows nur gebaut in CI, nicht ausgeführt.

## Offen
- ORC nach /Applications (Developer ID) nur auf Franks Wort; ad hoc signiert hält die Freigabe nicht.
- Rea-Sixty-dylib nicht installiert: Änderung dort nur Tabellen, ohne Wirkung (Rea-Sixty meldet media_ nicht an).
