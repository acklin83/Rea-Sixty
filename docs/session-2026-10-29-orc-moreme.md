# Session 29.10.2026: ORC MoreMe gebaut

Auftrag: stoerme (Browser-Kopfhörermixer) als **ORC MoreMe** in ORC, nach
`docs/orc-phones-plan.md`. Alle fünf Stufen gebaut, Stand und Abweichungen in Abschnitt 7.4
des Plans.

## Commits
- `e1a70aa2` Stufe 1: `orc/Phones.*`, `Manager::setRounds`, `RmeNames::toUtf8`, ctest `orc_phones`
- `30c5b58a` Stufen 2-4: `orc/WebSocket.*`, `orc/PhonesServer.*`, `orc/moreme/`, Reiter
  `orc/OrcPhonesTab.*`, Menüzeile + QR in `OrcApp.mm`, ctests `orc_websocket`, `orc_phones_server`
- `b1e868d2` Stufe 5: Handbuch, `orc_manual.py` prüft MoreMe-Menütexte

## Geprüft
- ctest 45/45. Absichtliche Brüche: Mute-Echo-Regel, Konfig-Dedup, OSC-Weiterleitung, alle drei
  fallen durch.
- Die echte Seite im Browser-Fenster gegen den echten Server und ein falsches TotalMix (Scratch-
  Programm, Ports vom System): PIN, Kopfhörerliste, Mischer mit Namen und Farben, Mute hält über
  eine /sendall-Runde, Unmute bringt -10 dB zurück. Nur deutsche Texte gesehen (Sprache des
  Fensters); die englische Tabelle hat dieselben 31 Schlüssel.
- `orc_manual.py --check` grün.
- ORC gebaut, nach /Applications (über /tmp + mv, Hash `614b843b…` gleich dem Build).

## Nicht geprüft
- Nichts am Gerät, kein TotalMix, kein ORC-Start (Regel). Offene Messungen: Plan Abschnitt 8
  (leitet TotalMix Remote 2 an Remote 3 weiter; Firewall-Frage beim ersten Handy).
- QR-Code nicht gescannt, Reiter nicht angesehen.

## Offen
- Frank: stoerme stoppen, ORC neu starten, MoreMe einschalten, PIN eintragen.
- stoerme-Repo stilllegen nach dem Umstieg (Entscheid 9.2).
- ORC-Release (Version, Build 8, Handbuch nach `ORC/docs`) ist nicht Teil dieser Session.
