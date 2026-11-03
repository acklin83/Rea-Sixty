# ORC: Settings als Seite, 03.11.2026

Plan: `docs/orc-settings-web-plan.md`. Frank: Leiste links, Off grau, `deskAllowed` vor dem Release, das neue Settings ins selbe Release. Danach, weil Frank weg war, über die Planungssession: bis zur Testreife fertig machen, nicht releasen, altes Fenster bleibt bis zu seinem Test.

## Gebaut
- `orc/SettingsServer`: nur 127.0.0.1, Port vom System, nur solange das Fenster offen ist, Zufallswert im Pfad, Host und Origin exakt mit Port.
- `orc/OrcSettingsModel`: alle Regeln, Zustand als JSON, eine Nachricht pro Änderung. Läuft auf dem Thread des Servers.
- `orc/settings/settings.html` und Schrift Inter im Bundle.
- `orc/OrcWebWindow.mm`: Fenster, Open at login, Logo-Dialog, Edit-Menü. `OrcDeskWindow.mm` ist weg, Studio Control steht auf dem Reiter MoreMe (iframe auf `/desk?embed=1`).
- Frank 03.11.: Namen für Phones 1-4 gestrichen (Antwort A), auch im alten Fenster. ⛔ Changelog-Zeile nötig: gespeicherte Namen wirken nicht mehr, ohne Performer steht dort der TotalMix-Name.
- `desk.html`: eigene Dialoge statt `confirm`/`prompt`, Menüs gleich hoch, Off grau, Schrift über `/fonts/`.
- `deskAllowed` vergleicht den Origin mit Port (`44c038f6`).
- Test `test_orc_settings`, Werkzeug `tools/orc_settings_demo.cpp`.

## Entschieden beim Bau, nach dem Plan
- Das alte Fenster bleibt unverändert unter `Settings (old)…`. Das Modell hat bis zur Entfernung eine Kopie der Regeln. Rückweg bleibt einfach, Stufe 6 löscht die Kopie.
- Keine gemerkte Fensterposition, wie im alten Fenster nicht. Der zuletzt offene Reiter bleibt bis zum Beenden von ORC.
- Statuszeilen, Adresse, Wearer und Stands laufen weiter, auch wenn ein Feld den Fokus hat. Alles andere wird erst neu gezeichnet, wenn Feld und Menü frei sind.
- OBS-Passwort: Feld zeigt acht Zeichen als Platzhalter, wird nie an die Seite geschickt. Beim Betreten ist alles markiert, Leeren und Verlassen löscht es.
- Neue Ablehnungen, wo vorher still etwas passierte: General-Ports und OBS-Port ausserhalb 1 bis 65535 geben die Fehlerzeile. Buchstaben in Zahlenfeldern ändern nichts mehr.
- Marke ist Inter 900 kursiv, Inter Tight lag nicht vor.

## Gefunden
- ⛔ `wdl_json_parser` liest `\\` als Anführungszeichen (`jsonparse.h`, Upstream). Die zwei Seiten schicken einen Backslash als `\`. Gilt auch für alle anderen, die mit dem Parser Texte lesen, nicht untersucht.
- `<select>` ignoriert in WebKit Padding und Höhe.
- `LINK_DEPENDS` kannte `desk.html` nicht: eine Änderung nur an dieser Seite blieb aus dem Bundle draussen. Ergänzt, mit `settings.html`.

## Geprüft
- `test_orc_settings`, ctest, `orc_manual.py --check`, `check_builtin_docs.py`.
- Seite gegen den echten Server, HOME auf Scratch, falsches TotalMix: WebKit offscreen und Chromium im Browser-Pane.
- Das echte `OrcWebWindow.mm` in einem Scratch-Bundle, Fenster ausserhalb des Bildschirms: lädt, Studio Control gesperrt bei MoreMe aus, Cmd-A über das Edit-Menü, Tippen im eigenen Prozess, offenes Feld beim Schliessen übernommen, Server weg nach dem Schliessen, Wiederöffnen auf dem letzten Reiter mit neuem Port.

## Nicht geprüft
ORC selbst wurde nicht gestartet (würde UF1, UF8 und den TotalMix-Remote übernehmen). Open at login, Logo-Dialog, Cmd-C und Cmd-V (hätte Franks Zwischenablage berührt), native Menüs per Maus, Hue mit echter Bridge, DynaMount mit echten Ständern, OBS, Import aus REAPER mit echten Werten, Studio Control mit echtem TotalMix, signierter Release-Build.

## Offen
- Stufe 6 nach Franks Test: `OrcSettingsWindow.mm`, `OrcModesTab`, `OrcPhonesTab`, `OrcLayout.h`, `Settings (old)…`, CMake, Kommentare in `main.cpp`, `OrcUi.h`, `tools/orc_manual_dump.cpp`.
- Nur Frank: `rme.json` wird nicht atomar geschrieben, fixen oder nicht.
- Windows-Milestone: Plan Abschnitt 4.
