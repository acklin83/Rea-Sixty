# Plan: das ORC-Handbuch

Stand 29.09.2026. Nur Plan, nichts geschrieben. Frank: „Handbuch brauchen wir ein schlaues!"

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
