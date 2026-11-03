# MoreMe: Performer und Studio Control, Plan

Frank 03.11.2026, angenommen mit allen Vorschlägen. Gebaut wird nach seinen Tests und dem nächsten ORC-Release, vor ORC für Windows.

## Ziel
Jeder Musiker bekommt sein eigenes Layout: eigener Ausgang, eigene Fader, eigene Freigaben, eigener Link. Der Tontechniker richtet das auf einer Seite ein.

## Entschieden
- Studio Control ist eine lokale Seite, die ORC selbst ausliefert (`/desk`), nichts im Internet. Am Mac öffnet sie sich in einem eigenen ORC-Fenster mit Web-Ansicht, vom Tablet über die Adresse des Macs. Grund: läuft unter Windows unverändert, geht vom iPad, Vorschau der Handyseite direkt daneben.
- `/desk` hat einen eigenen PIN und antwortet nur im lokalen Netz. Anfragen über den Reverse Proxy (X-Forwarded-For, X-Real-IP) oder von aussen werden abgewiesen.
- Der Reiter MoreMe in ORC behält ein/aus, Ports, PIN, Studioname und Logo und bekommt den Knopf Studio Control, auch im ORC-Menü.
- QR-Code und Link pro Performer. Der Handy-PIN gilt weiter, nur beim ersten Mal, das Handy merkt ihn sich. Der QR-Code enthält nie den PIN.
- Ohne Performer bleibt die heutige Liste Phones 1-4.
- EQ, Dynamics und Auto Level auch auf dem eigenen Ausgang (Master-Fader): wirkt nur auf diese Kopfhörer.
- EQ, Dynamics und FX Send auf Kanälen wirken auf alle, die den Kanal hören, und auf die Aufnahme: ab Werk gesperrt, Freigabe pro Performer Off, Simple, Advanced.

## Stufen
1. Performer und Studio Control.
   - Performer: Name, Rolle, Ausgang (Rolle oder fester Ausgang, `rmeu::keyOutput`), Fader (Quelle Input oder Playback, Name, Farbe, Reihenfolge), Master an/aus, Freigaben, Link-Kennung. Gespeichert in `moreme.json`.
   - Seite in drei Spalten: Performer, Einstellungen, QR und Link (Kopieren, Teilen, Vorschau). Oben Snapshots: New, Save, Save as. Schnellknöpfe für häufige Fader.
   - Jede Änderung sofort gespeichert und sofort auf dem Handy.
2. Handyseite: Link öffnet das eigene Layout, Querformat mit festem Master, Hell/Dunkel, Doppeltipp setzt zurück.
3. EQ: Grafik zum Ziehen mit Q und Werten, einfache Stufe Bass/Mid/Treble, Bypass. Kurve aus `Uf1EqCurve` auf dem Server, keine zweite Kopie in JavaScript. Parameter aus dem Katalog in `RmeStrip.cpp`.
4. Dynamics und Auto Level: Kompressor, Expander, Auto Level mit Kennlinie, einfache Stufe. FX Send (`/input/<n>/fxsend`) gesperrt mit dabei.
5. Tests (nur Ports vom System, nie 7003/7004 auf Franks Mac), Handbuch, Website, alle Sprachen der Seite.

## Lassen wir weg
Geführte Tour, Themes im Pult, Gerätelisten: MoreMe liest die Kanäle aus TotalMix.

## Grenzen
- Was im TotalMix-Layout versteckt ist, steht nicht zur Wahl, auch bei Ausgängen.
- Mono/Stereo kommt aus TotalMix, nicht aus einer eigenen Einstellung.
- Gain-Reduction-Meter nur, falls TotalMix einen Wert meldet. In den Läufen vom 03.11. keine Adresse, Send Peak Level war aus. Im Studio messen, sonst nur die Kennlinie.

## Vor dem Bau zu klären
- Welche Bibliothek die Web-Ansicht unter Windows braucht, gehört in den Windows-Plan.
- `orc-phones-plan.md` und der Wechsel von „alle sehen dasselbe“ auf Layouts: `visible()` bleibt die Quelle dessen, was wählbar ist.
