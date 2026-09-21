# UF1 DAW-Ansicht auf Layout 1: der ganze Plan

Stand 21.09.2026. **Nichts davon ist gebaut.**

⛔ **GEPARKT, Frank 21.09.:** „lassen wir das vorerst. Ich hab lieber einen EQ
graph auf jeder Seite, bringt ja nicht viel ausser farbbalken für
nachbarkanäle. Plan behalten." Nicht wieder anbieten, bis Frank ihn aufmacht.

## Worum es geht

Die DAW-Ansicht der UF1 in REAPER, so wie sie heute ist (vier Fensterspuren auf
den V-Pots, Name und dB, Segmentleiste, 5-8-Taste, Soft-Key-Bänke), zieht von
Layout 3 auf **Layout 1** um. Dort gibt es keinen EQ-Graph, dafür **vier
Farbbalken über den V-Pots**, und die bekommen die **REAPER-Spurfarbe** der
jeweiligen Fensterspur.

Nicht gemeint: TotalMix (eigener Plan `docs/uf1-spread-plan.md`), das
Standalone ORC.

## Was sich am Gerät ändert

| | heute (Layout 3) | neu (Layout 1) |
|---|---|---|
| V-Pots: Name, dB, Segmentleiste | ja | ja |
| Farbbalken pro Spur | nein | **ja, Spurfarbe** |
| EQ-Graph | ja | **nein** |
| Kopfzellen | CELL1 *channel*, CELL4 *soft key*, CELL5 *fine ctrl* | CELL1 *channel* (10 Zeichen), CELL2 *soft key* (5 Zeichen) |
| Zeitfeld | ja | ja (auf Foto 4678 in Layout 1 gesehen) |
| Soft-Keys | 13 Zeichen | 12 Gross- / 15 Kleinbuchstaben |
| kleine Anzeige am Fader, Fader, LEDs | unverändert | unverändert |

Plugin-, Sends- und Meter-Ansicht bleiben, wo sie sind.

## Die Teile

### 1. Die Ebene gehört zur Ansicht

Heute teilen Plugin, DAW und Sends eine Ebene (`{03,00}`), darum ist ein
Wechsel zwischen ihnen nur `changed` und nicht `layoutChanged`
(`main.cpp:33611`, Kommentar dort). Das stimmt dann nicht mehr:

- `layoutChanged` (`main.cpp:33767`) bekommt „DAW betreten oder verlassen"
  dazu. Nur das, kein anderer Unteransichtswechsel, sonst kommt das August-
  Problem zurück (Memory `uf1-mode-edge-must-not-relayout`: nur echte
  Ebenenwechsel dürfen `0x0100` neu senden).
- Der Kanal-Burst (`main.cpp:33870` ff.) schreibt heute fest `{03,00}`. Er
  bekommt einen DAW-Zweig: zweistufig `{00,01}`, `{00,01}`, dann `{01,00}`,
  wie die Sonde. `0x011a` bleibt auf `02` (auf `FF` verschwinden CELL1/CELL2).
- Jeder, der die Ebene zurückholt (`g_uf1PlaneLost` nach Hue, Side-Car, Sonde,
  Preset-Browser), landet damit automatisch auf der richtigen Ebene, weil er
  denselben Burst auslöst.

### 2. Farbbalken

`0x012b`, vier Bytes, ein Palettenindex pro Fensterspur:
`uf8::quantize(Spurfarbe)`, genau wie der kleine Balken am Fader
(`main.cpp:32000`). Dieselbe Palette, Frank 21.09. Keine Spur im Fenster oder
Spur ohne Farbe: `0` (aus). Nur senden, wenn sich einer der vier Werte ändert.

### 3. V-Pot-Reihe

Der DAW-Zweig (`main.cpp:34620` ff.) bleibt, zwei Dinge ändern sich:

- **Stil.** `setBar` (`main.cpp:34575`) führt zu `uf1VpotBar_`, und das setzt
  `0x01` (unipolar). In Layout 1 blendet `0x01` die ganze Reihe aus. Im
  DAW-Zweig daher `0x02` (Füllung von links) oder `0x04`, Frage B. Leere Slots
  bleiben `0x03`, das rendert in Layout 1 (Text ja, Leiste nein).
- **Wertzeile.** `uf1ValueLine` baut 11 + 8 = 19 Zeichen, in Layout 1 fällt das
  letzte Zeichen weg („-12.3dB" würde zu „-12.3d"). Für den DAW-Zweig eine
  Layout-1-Fassung, deren Wert eine Stelle früher endet. Frage C klärt vorher,
  wo die Zone für den Wert in Layout 1 anfängt.
- `uf1EmitVpotRow_` bleibt der einzige Schreiber der Reihe.

### 4. Kopfzeile

`uf1BuildLiveHeader_` (`main.cpp:23646`) füllt die Zellen für Layout 3. Der
Pacer schickt sie alle 40 ms. In Layout 1 werden nur Zelle 1 und 2 gezeigt,
darum im DAW-Fall:

- CELL1 (10 Zeichen): was heute unter *channel* steht (REAPER bzw. ENC-/JOG-Modus)
- CELL2 (5 Zeichen): die Soft-Key-Bank `N/M`, heute unter *soft key*
  (`10/10` passt genau)
- die Fine-Anzeige (*fine ctrl*) hat in Layout 1 keinen Platz, Frage D

### 5. EQ-Graph und Pacer

- `uf1PaintEqGraph_` (`main.cpp:28732`, aufgerufen `main.cpp:35407`) läuft im
  DAW-Fall nicht.
- Der Pacer schickt pro Zyklus Bildstücke (`0x0122`), Pegel und Kopfzeile. Die
  Sonde hat ihn in Layout 1 angehalten, **mit laufendem Pacer ist Layout 1 nie
  gesehen worden**. Der Pacer existiert, weil die UF1 ohne gleichmässigen Takt
  auf „schwarzes Gesicht" zurückfällt (Kommentar `main.cpp:23686`). Also:
  Takt behalten, im DAW-Fall ohne Bildstücke. Messung E vorher.

### 6. Soft-Keys

Beschriftungen haben heute 13 Zeichen (`kUf1SoftKeyChars`), Layout 1 zeigt
12 Grossbuchstaben. Im DAW-Fall auf 12 kürzen (gleiche Abkürzung, nur eine
Stelle weniger). LEDs und Hervorhebung `0x0102` bleiben.

### 7. Tests

Was sich ohne Gerät prüfen lässt, wird ein Test: die vier Farbindizes aus vier
Spurfarben, die Layout-1-Wertzeile (Länge, Ausrichtung), die Belegung der
Kopfzellen im DAW-Fall. CI fährt kein ctest, also lokal.

### 8. Doku

Handbuch-Kapitel UF1 DAW-Ansicht, Memory, Session-Notiz. Die Notion-Seite des
Video-Skripts nicht, Frank spricht kein Kapitel vor 5.12 neu ein.

## Messungen am Gerät vor dem Bau

| | Frage | wie |
|---|---|---|
| C | Wo beginnt in Layout 1 die Zone für den Wert? | Sonde: Label `ABCDEFGHIJK` + Wert `1234567` |
| E | Wie sieht Layout 1 mit laufendem Pacer aus, mit und ohne Bildstücke? | Sonde mit Pacer an |
| F | Zeigt Layout 1 „SOLO ACTIVE" (`0x0120`)? | Sonde, ein Element |

Alle drei laufen mit der bestehenden Sonde plus drei kleinen Elementen.

## Offene Entscheidungen für Frank

- **A. Ersetzen oder wählbar?** Die DAW-Ansicht wandert fest auf Layout 1,
  oder eine Einstellung „DAW-Ansicht mit EQ-Graph / mit Farbbalken"?
  Vorschlag: fest, eine Ansicht, nichts doppeln.
- **B. Stil der Segmentleiste:** `0x02` Füllung von links (wie ein Pegel,
  Foto 4679) oder `0x04` Kästchen (Foto 4678)?
- **D. Fine-Anzeige:** ohne Kopfzelle ist Fine nur noch über die Soft-Key-LED
  sichtbar, falls Fine auf einem Soft-Key liegt. Reicht das?

## Was NICHT drin ist

- Layout 2, das Zahlenfeld `0x011b`
- eine zweite Textzeile pro Pot (`0x010b`), von Frank verworfen (`a971ebb`)
- TotalMix, Side-Car, ORC
- UF8, UC1, Extender

## Reihenfolge

Messungen C, E, F. Dann 1 (Ebene), dann 5 (Pacer), dann 2, 3, 4, 6 zusammen,
dann 7, dann 8. Die Ebene zuerst, weil jeder spätere Schritt nur auf Layout 1
prüfbar ist.

## Was belegt ist und was nicht

Belegt am 21.09. (grep oder Gerät): alle Datei:Zeile-Angaben, die Breiten und
Stile aus dem Runbook, dass das Zeitfeld in Layout 1 zeichnet (Foto 4678), dass
die Sonde den Pacer anhält (`uf1HandOverScreen_`, `g_uf1CycleActive`).

Nicht belegt: ob die kleine Anzeige am Fader und die LEDs von der Ebene
unabhängig sind (bei der Sonde nicht angesehen), wie Layout 1 mit Pacer
aussieht, wo die Wertzone beginnt.
