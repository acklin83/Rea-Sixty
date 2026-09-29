# Plan: das ORC-Handbuch

Stand 29.09.2026: **gebaut.** Frank: „Handbuch brauchen wir ein schlaues!", Plan freigegeben
(„ja, passt so. nur englisch, rea-sixty darf rein").

## Gebaut

- `extension/tools/orc_manual_dump.cpp` (Target `orc_manual_dump`, gleiche Quellen wie ORC über
  `ORC_SHARED_SOURCES`): gibt als JSON aus, was ORCs Code weiss. Aktionen (Kategorie RME),
  Werksbänke (in einem Wegwerf-Ordner geseedet), STRIP-Seiten, jeder Regler mit Bereich und dem
  Wert, den ein Druck setzt (echt über `resetWrites` gefragt), Reihen, Vorgaben, Push-Sperre.
- `extension/tools/orc_manual.py`: baut `extension/build/orc-manual/index.html` aus
  `extension/orc/manual/template.html` und den Daten. Liest Version, Mindest-macOS,
  libusb-Version und Fine-Faktor aus den Quellen. **Schlägt fehl**, wenn eine UF1-Taste oder ein
  Drehgeber aus `UF1Protocol.h` keinen Abschnitt hat (`data-btn` / `data-enc`), wenn ein Text der
  Menüleiste fehlt (aus `linkSummary`, `RmeManager::setStatus`, `Surface.cpp`, `OrcApp.mm`), oder
  wenn ein Platzhalter leer bleibt. In der CI (macOS arm64, „Verify the ORC manual").
- Seite: eigene UF1-Zeichnung (SVG, klickbar, nach SSLs Anordnung, keine SSL-Grafik), Suche,
  hell/dunkel, Handybreite geprüft. Gepackte Seitenläufe als eine Tabelle in Reihenfolge.
- ORC-Menü „Manual" öffnet `https://acklin83.github.io/ORC/` (lebt ab Release-Schritt 9).
- Rea-Sixty-Handbuch: kurzer Absatz „TotalMix (RME)" unter UF1 Views mit Verweis (Frage 4 von
  Frank nicht beantwortet, kleinster Schritt genommen).

## Ursprünglicher Plan

## Ausgangslage

- Das ORC-Fenster erklärt nichts, auf Franks Wunsch (keine Hilfesätze). Alles, was ein
  Nutzer wissen muss, steht also im Handbuch oder nirgends.
- Rea-Sixtys Handbuch hat **kein Kapitel zum RME-Side-Car**. Der Side-Car ist derselbe Code
  wie ORC auf der Fläche. Ein ORC-Handbuch deckt ihn für beide Gruppen ab.
- Was ORC anbietet (nachgesehen im Code, 29.09.):
  - Menüleiste: Zeile „UF1" (wer ihn hält), Zeile „TotalMix" (Verbindung), „Settings…",
    „Quit ORC".
  - Fenster: vier Seiten, Connection (mit „Open at login"), Controls, Soft Keys, Colours.
  - Fläche: vier Reihen (Input, Playback, Output, FX), Kanal-Encoder, Fader, Pan-/Width-Pot,
    CUT / SOLO / SEL, V-Pots 1-4 mit zwei Bänken (5-8), Jog, Nav-Kreuz, MASTER, STRIP mit
    seinen Seiten (EQ-Graph auf Layout 3), Soft-Key-Bänke mit 14 RME-Aktionen,
    Push-Schutz 250 ms.
  - Statuswörter: off, waiting for TotalMix, online, port busy, silent, „handed over to
    REAPER".

## Was „schlau" heissen soll (Vorschlag, Frank entscheidet)

1. **Nach der Fläche geordnet, nicht nach dem Fenster.** Ein Bild der UF1, jedes
   Bedienelement anklickbar. Dahinter, was es tut, **pro Reihe**: derselbe Pot ist auf
   einem Eingang Pan und auf FX Width, CUT ist Mute oder „Effekt aus". Eine Tabelle pro
   Element statt Fliesstext, weil das Verhalten von der Reihe abhängt.
2. **Aus dem Code erzeugt, wo der Code die Antwort kennt.** Die Listen, die sonst
   auseinanderlaufen, schreibt ein Skript aus den Quellen:
   - die Soft-Key-Aktionen mit Beschreibung (`kBuiltinDocs` in `Bindings.cpp`),
   - die STRIP-Seiten und jeder Regler mit Bereich und Einheit (Katalog in `RmeStrip.cpp`,
     Werksseiten in `RmeManager.cpp`),
   - die Reverb-Typen und welche Regler sie zeigen (`revTypes`),
   - die Statuswörter aus der Menüleiste.
   Eine Prüfung in der CI schlägt fehl, wenn im Code etwas dazukommt, das im Handbuch fehlt
   (so wie `check_builtin_docs.py` für die Built-ins).
3. **Zwei Einstiege für die zwei Gruppen.** „Nur ORC": TotalMix einrichten (Global OSC,
   Remote auf 7005 / 7006, SSL 360 zu), UF1 anstecken, fertig. „ORC mit Rea-Sixty": die
   Übergabe beim REAPER-Start und -Ende, eine gemeinsame Einstellungsquelle.
4. **Fehlersuche vom Status aus.** Jedes Wort, das die Menüleiste zeigen kann, mit dem, was
   es heisst und was zu tun ist.
5. **Aus ORC erreichbar.** Ein Eintrag „Manual" im Menüleisten-Menü öffnet das Handbuch.
   Kein Satz im Fenster.

## Form und Ort (Vorschlag)

- **Eine Webseite im Repo `acklin83/ORC`**, veröffentlicht über GitHub Pages. Eine Seite,
  im Browser durchsuchbar, das UF1-Bild darin als SVG. Kein Server, kein externer Dienst.
- **Englisch.** Rea-Sixtys Handbuch ist englisch, die Nutzer sind international.
- Geschrieben mit dem Skill `texte`: ich-Form, kein „wir", keine Behauptung ohne Quelle.

## Fragen an Frank

1. Trifft „schlau" das, was du meinst (Punkte 1 bis 5)? Oder meinst du etwas anderes,
   zum Beispiel Hilfe direkt auf der UF1-Anzeige?
2. Englisch allein, oder auch Deutsch?
3. Darf das ORC-Handbuch Rea-Sixty beim Namen nennen (Einstieg für Gruppe B)? Das
   ORC-Fenster tut es auf deinen Wunsch nicht.
4. Soll Rea-Sixtys Handbuch für den Side-Car auf das ORC-Handbuch verweisen, statt ein
   eigenes Kapitel zu bekommen?

## Was ich nicht mache

- Keine Hilfetexte im ORC-Fenster.
- Kein Video.
