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

## 16.11.2026: Installation, Messung, Release 1.7.0

- Dev-Build `df33d36c` mit Developer ID nach /Applications (ORC beendet, Anmeldeobjekt blieb).
- Frank: System volume geht bei ihm nicht. Gemessen per Core-Audio-Abfrage (nur lesen): Fireface UFX+ hat weder virtuelle Hauptlautstärke noch Kanallautstärke. Handbuch-Satz `5bc65110`: bei RME den Jog auf die TotalMix-Ausgabe.
- ORC 1.7.0 (Build 15, `b794ec99`) released: https://github.com/acklin83/ORC/releases/tag/v1.7.0. App + DMG notarisiert, Windows-Installer aus CI, Handbuch ORC `5608b90`, Website-Knopf `413ad2e4` (private, Deploy durch Frank).
- Offen: /Applications noch nicht aus dem DMG (ORC lief), Website-Deploy.
