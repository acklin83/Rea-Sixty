# MoreMe: Performer und Studio Control, Plan

Frank 03.11.2026, angenommen mit allen Vorschlägen. Gebaut 03.11. (Frank: kommt ins selbe Release wie die Submix-Tasten), vor ORC für Windows.

## Ziel
Jeder Musiker bekommt sein eigenes Layout: eigener Ausgang, eigene Fader, eigene Freigaben, eigener Link. Der Tontechniker richtet das auf einer Seite ein.

## Entschieden
- Studio Control ist eine lokale Seite, die ORC selbst ausliefert (`/desk`), nichts im Internet. Sie öffnet sich in einem eigenen ORC-Fenster mit Web-Ansicht, auf dem Mac (Frank 03.11.: „mach ich doch über das menu am mac, nie am phone/tablet“). Grund für die Seite: läuft unter Windows unverändert, Vorschau der Handyseite direkt daneben.
- `/desk` antwortet nur auf dem Mac selbst (127.0.0.1) und braucht darum keinen PIN. Anfragen übers Netz, über den Reverse Proxy (X-Forwarded-For, X-Real-IP) oder von einer fremden Seite im Browser werden abgewiesen.
- Der Reiter MoreMe in ORC behält ein/aus, Ports, PIN, Studioname und Logo und bekommt den Knopf Studio Control, auch im ORC-Menü.
- QR-Code und Link pro Performer. Der Handy-PIN gilt weiter, nur beim ersten Mal, das Handy merkt ihn sich. Der QR-Code enthält nie den PIN.
- Ohne Performer bleibt die heutige Liste Phones 1-4.
- Fader pro Performer mit einem Schalter: TotalMix-Layout oder Eigen. Neue Performer starten auf TotalMix-Layout.
  - TotalMix-Layout folgt laufend den sichtbaren Kanälen mit ihren Namen und Farben, Inputs dann Playbacks. Das ist der Satz, den die Seite heute schon zeigt (`Mixer::sourcesJson_`).
  - Eigen startet als Kopie des Layouts, wenn die eigene Liste leer ist. Der Knopf Copy TotalMix layout überschreibt die Liste. Zurück auf TotalMix-Layout lässt die eigene Liste stehen.
- Der Performer wählt am Handy selbst: Streifen oder Liste, Hell oder Dunkel, Meter. Das gilt nur für sein Handy und ändert nichts an TotalMix.
- Freigabe pro Performer, ob er Fader auf seinem Handy ausblenden und umsortieren darf: Off (ab Werk) oder Hide. Gespeichert auf dem Handy, Studio Control und die Vorschau zeigen weiter das eingerichtete Layout. Bei Off gilt das eingerichtete Layout, die gespeicherte Auswahl bleibt für später liegen. Kanäle hinzufügen kann nur der Tontechniker.
- Fader hinzufügen: Liste aller sichtbaren Kanäle, die dieser Performer noch nicht hat, mit TotalMix-Namen, getrennt nach Inputs und Playbacks. Ein Fader ist ein Kanal, keine Gruppen. MoreMe kennt nur, was TotalMix liefert.
- EQ, Dynamics und Auto Level auch auf dem eigenen Ausgang (Master-Fader): wirkt nur auf diese Kopfhörer.
- EQ, Dynamics und FX Send auf Kanälen wirken auf alle, die den Kanal hören, und auf die Aufnahme: ab Werk gesperrt, Freigabe pro Performer Off, Simple, Advanced.

## Stufen
1. Performer und Studio Control.
   - Performer: Name, Rolle, Ausgang (Rolle oder fester Ausgang, `rmeu::keyOutput`), Fader (TotalMix-Layout oder eigene Liste: Quelle Input oder Playback mit TotalMix-Namen, Name und Farbe vorbelegt aus TotalMix und änderbar, Reihenfolge), Master an/aus, Freigaben, Link-Kennung. Gespeichert in `moreme-performers.json`, damit nur der Server-Thread diese Datei schreibt und der Reiter MoreMe (`moreme.json`) nie dagegen läuft.
   - Seite in drei Spalten: Performer, Einstellungen, QR und Link (Kopieren, Teilen, Vorschau). Oben Snapshots: New, Save, Save as.
   - Performer-Liste mit Kurzstatus: Ausgang, Anzahl Fader oder TotalMix-Layout, EQ- und DYN-Stufe.
   - Fader sortieren mit Griffen per Drag and Drop, auch am Handy (Frank 03.11.).
   - Jede Änderung sofort gespeichert und sofort auf dem Handy.
2. Handyseite: Link öffnet das eigene Layout, Hell/Dunkel, Doppeltipp setzt zurück.
   - Master: bleibt die vorhandene Einstellung Master fixieren (`LS.sticky`, `getStickyPref`). Ab Werk neu an, heute aus. In der Liste oben, bei den Streifen im Hoch- und Querformat rechts statt heute links, der Text der Einstellung in allen Sprachen zieht mit.
   - Ein neues Handy startet im Hochformat mit der heutigen Liste. Senkrechte Streifen auch im Hochformat sind die Wahl am Handy. Die Faderlänge passt sich dem Bildschirm an, gescrollt wird nur seitwärts.
   - dB-Skala direkt am Fader.
   - EQ, DYN und FX Send erscheinen am Streifen nur, wenn sie freigegeben sind und der Kanal den Parameter hat (`strip::available`, keine feste Liste pro Kanalart).
   - Kopfzeile mit Name, Rolle, Ausgang und dem Knopf Hell/Dunkel.
   - Hinweis, wenn rechts noch mehr Fader kommen.
   - Pan, Mute, Solo und die Meter bleiben.
   - Die Wahl Streifen oder Liste und, wenn freigegeben, die Liste Meine Fader mit Ausblenden und Pfeilen liegen in den vorhandenen Einstellungen (Zahnrad).
3. EQ: Grafik zum Ziehen mit Q und Werten, einfache Stufe Bass/Mid/Treble, Bypass. Kurve aus `Uf1EqCurve` auf dem Server, keine zweite Kopie in JavaScript. Parameter aus dem Katalog in `RmeStrip.cpp`.
4. Dynamics und Auto Level: Kompressor, Expander, Auto Level mit Kennlinie, einfache Stufe. FX Send (`/input/<n>/fxsend`) gesperrt mit dabei.
5. Tests (nur Ports vom System, nie 7003/7004 auf Franks Mac), Handbuch, Website, alle Sprachen der Seite.

## Lassen wir weg
Geführte Tour, Themes im Pult, Gerätelisten: MoreMe liest die Kanäle aus TotalMix.

## Grenzen
- Was im TotalMix-Layout versteckt ist, steht nicht zur Wahl, auch bei Ausgängen.
- Wird ein Kanal einer eigenen Liste in TotalMix versteckt, verschwindet der Fader vom Handy und bleibt gespeichert. Sobald der Kanal wieder sichtbar ist, ist er wieder da.
- Mono/Stereo kommt aus TotalMix, nicht aus einer eigenen Einstellung.
- Gain-Reduction-Meter nur, falls TotalMix einen Wert meldet. In den Läufen vom 03.11. keine Adresse, Send Peak Level war aus. Im Studio messen, sonst nur die Kennlinie.

## Vor dem Bau zu klären
- Welche Bibliothek die Web-Ansicht unter Windows braucht, gehört in den Windows-Plan.
- `orc-phones-plan.md` und der Wechsel von „alle sehen dasselbe“ auf Layouts: `visible()` bleibt die Quelle dessen, was wählbar ist.
