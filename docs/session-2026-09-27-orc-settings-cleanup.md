# Session 27.09.2026 abends: ORC-Einstellungen aufgeräumt, 5-8 schaltet die Hälfte

Franks Meldung mit vier Bildschirmfotos des ORC-Einstellungsfensters.

## Entscheidungen (Frank, auf Nachfrage)

- **5-8 schaltet die Hälfte**, zusätzlich zu Bank ◄ ►. Lampe: gedimmt = es gibt eine
  zweite Hälfte, hell = Tasten 5-8 sichtbar, dunkel = keine zweite Hälfte oder STRIP.
- **Farbnamen und Farbfelder aus SSL 360s eigener Liste** (Issue #8, Abschnitt C,
  `LedColourType`). 0x01 heisst jetzt White und zeigt weiss, obwohl der UF8 ihn
  hellblau zeigte.

## Was gebaut wurde

| Teil | Wo |
|---|---|
| 5-8 in beiden Programmen | `RmeSoftKeys::stepKey`, Lampe in `RmeFace::keyLamps` |
| Tab-Leiste ohne Fokusring | `tabs.focusRingType` |
| Alle Hilfesätze raus, keine Rea-Sixty/REAPER-Bezüge mehr im Fenster | `OrcSettingsWindow.mm` |
| Kanalzählung „INPUT 18 PLAYBACK 1 OUTPUT 5“ raus | dito |
| Status als eine Zeile „Status: Online“ statt „TotalMix: online connected to TotalMix (…)“; Menüleiste gleich | `orc::linkSummary` |
| Controls: Spalte „turn“ weg (nur Lautstärke), Bänke als Überschrift, Ziele als Wörter („Phones 1“, Kanäle unter Inputs/Playback/Outputs), Push als „Submix/Select/Mute/Nothing“, dB als Einheit hinter dem Feld | dito |
| Soft Keys: Aktionen ohne „RME: “, Tastennummern 5-8 auf der zweiten Hälfte | dito |
| Colours: SSL-Namen, SSL-Farbfelder, keine Hex-Zahlen, nur die 13 Einträge, die leuchten (vorher dreimal „off“ zusätzlich), TotalMix-Farben gross geschrieben ohne OSC-Nummer | `Palette.cpp` (`paletteName`, neu `paletteSwatch`) |
| Grids nicht mehr auf Fensterbreite gestreckt (Lücke zwischen „key“ und „label“) | `pageWithStack` |
| Port-Meldung ohne „stoerme, TotalReaper or a second REAPER?“ | `RmeManager.cpp` |

`paletteName` hat nur ORC als Leser (gegreppt). Der Quantisierer (`paletteEntry`,
`kPalette`) ist unverändert.

## Geprüft

- ctest 17/17. Der neue 5-8-Test fällt mit ausgeschaltetem Zweig um (7 Fehler).
- Alle vier Seiten offscreen gerendert (Prüfprogramm mit den ORC-Objekten, Kopie von
  `orc.json`, ohne TotalMix-Verbindung) und angesehen. Dabei zwei Layoutfehler
  gefunden und behoben (Streckung, gequetschtes Host-Feld).
- NICHT gesehen: die Tab-Leiste selbst (fehlt im Offscreen-Bild) und die Zielmenüs mit
  Kanal-Überschriften (brauchen eine TotalMix-Verbindung). 5-8 am Gerät ungesehen.

## Offen

- ORC-Menü „UF1 not open: handed over to REAPER“ nennt REAPER. Nur sichtbar, wenn
  Rea-Sixty den UF1 übernimmt; nicht angefasst.
