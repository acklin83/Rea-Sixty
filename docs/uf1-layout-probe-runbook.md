# UF1 layout probe — was morgen am Geraet passiert

Gebaut 19.09.2026, deployt auf **Frank's Mac Studio**
(`~/Library/Application Support/REAPER/UserPlugins/reaper_rea-sixty.dylib`).
REAPER einmal neu starten, dann steht sie in **Settings, About, "UF1 display
probe"**.

## Worum es geht, in drei Saetzen

Frank hat auf dem UF1 in SSLs eigenem DAW-Mode zwei Dinge gesehen, die wir
nicht haben: ein grosses Textfeld dort, wo bei uns der EQ-Graph sitzt, und
einen Farbbalken pro V-Pot. Beides ist **kein neues Element**, sondern liegt
hinter einer **Layout-Ebene, die wir nie einschalten**: `0x0100` ist der
Layout-Selektor des grossen LCD, zwei Byte `{layout, screen}`, und wir fahren
nur `{03,00}` (Kanal) und `{04,00..05}` (Meter). Der ganze Capture-Korpus
kennt keine anderen, SSLs DAW-Layer wurde nie aufgezeichnet.

Die Sonde bewegt **genau diesen Selektor**, nicht irgendwelche unbekannten
Elemente. Das ist die zahme Variante: es ist dieselbe Operation wie jeder
MODE-Wechsel, und den Rueckweg geht der Maler ohnehin auf jeder Flanke.

## Was die Sonde tut, wenn sie an ist

1. schreibt `0x0100 = {layout, screen}` mit den eingestellten Bytes
2. schreibt acht nummerierte Textzellen (`CELL1` bis `CELL8`) in die Kopfzeile
   `0x011c`, damit eine Ebene, die dort Text rendert, es auch sagt
3. schreibt `00 01 02 03` in die vier Vier-Byte-Kandidaten, in dieser
   Reihenfolge: `0x0121`, `0x0113`, `0x0118`, `0x012b`

**⛔ Das Muster sind Positionen, keine Farben.** Die Frage lautet "wie viele
Balken und wo", nicht "welche Farbe". Frank ist farbenblind, eine Farbantwort
haette den falschen Ableser.

## Reihenfolge morgen

**0. Kontrolllauf in der bekannten Ebene.** Sonde an, Layout 3, Screen 0.
Das ist die Ebene, in der wir ohnehin sind. Was hier von `CELL1..8` und den
vier Kandidaten zu sehen ist, ist die **Grundlinie**: alles, was in einer
anderen Ebene mehr oder anders ist, ist der Befund.

**1. Die Ebenen abgehen.** Layout 1, dann 2, dann 5, 6, 7. Screen jeweils 0.
Nach jedem Schritt hinsehen: aendert sich das Layout ueberhaupt? Erscheinen
die Zellen? Erscheinen Balken, und wie viele?

**2. Bei einer Ebene, die anders aussieht: Screens durchgehen.** Screen 1, 2,
3 innerhalb desselben Layouts, so wie die Meter-Ebene sechs Screens hat.

**3. `{0x80, 0x03}`.** Das ist der einzige andere Wert im ganzen Korpus, ein
einziges Mal, ganz am Anfang des Kaltstarts (cap101, Frame 585, noch vor dem
ersten `{03,00}`). Vermutlich kein Layout, sondern eine Ansage. Zuletzt, weil
am wenigsten verstanden.

## Wenn etwas haengt

Der Reihe nach, nicht durcheinander:

1. Haken **"Take over the UF1 screen"** aus. Das setzt `g_uf1PlaneLost`, und
   der Kanalmaler baut die Ebene im naechsten Tick komplett neu auf.
2. **MODE am Geraet** druecken. Das schaltet die Firmware-Ansicht und loest
   bei uns denselben Wiederaufbau aus.
3. REAPER neu starten.
4. UF1 ab- und wieder anstecken.

Die Sonde fasst `0x011b` **nicht** an. Das ist das eine Element, das die
Kanalansicht nachweislich zerlegt hat, und es ist nicht in ihrer Liste.

## Wenn eine Ebene gefunden ist

Dann erst die Elemente darin abklopfen, und zwar in dieser Reihenfolge:

1. Welche der vier Kandidaten rendern dort ueberhaupt etwas?
2. Fuer den, der rendert: Wertebereich abtasten (0, 1, 2, 4, 8, 15, 255 auf
   allen vier Byte gleichzeitig), um Stil von Farbindex zu trennen.
3. Das grosse Textfeld: wenn `CELL1..8` in `0x011c` dort nicht erscheinen,
   sitzt der Text in einem anderen Element. Dann die Adressen zwischen `0x0120`
   und `0x012b` mit ASCII bewerfen, nicht mit Zahlen.

## Danach

Das Ergebnis gehoert in `docs/uf1-sidecar-rme-feasibility.md`, Abschnitt 14,
und in die Memory `uf1-screen-elements-we-dont-drive`. Beide sagen heute
"unbekannt" und sollen morgen etwas anderes sagen.

---

# Die Karte, aufgenommen am 21.09.2026

Franks Durchgang durch `(Layout, Screen)`, mit angehaltenem Pacer (also ist
sichtbarer Inhalt der der Sonde). `CELLn` = Zelle *n* der Kopfzeile `0x011c`.

| L | S | was zu sehen ist |
|---|---|---|
| 0 | 1 | SSL-UF1-Startbildschirm, `CELL1` unter dem Logo |
| 1 | 0 | farbige Balken unter der Zeitzeile, `CELL1` links unter *channel*, `CELL2` rechts unter *soft key* |
| 1 | 1 | wie S0, aber **ohne** die Beschriftungen und ohne CELL1/CELL2 |
| 1 | 2 | wie S0, aber *plugin* und *insert slot* statt *channel* und *soft key* |
| 1 | 3 | wie S0, plus eine **grüne Linie unter CELL1** |
| 2 | 0 | Zeitzeile plus **2-stelliges 7-Segment**; *channel* = CELL1, *page* = CELL3, *soft key* = CELL2 |
| 2 | 1-2 | identisch zu S0 |
| 2 | 3 | wie S0, aber **4-stelliges** 7-Segment, plus grüne Linie unter CELL1 bis CELL3 |
| 3 | alle | unsere heutige Kanalansicht: CELL1 unter *channel*, CELL4 unter *soft key*, CELL5 unter *fine ctrl* |
| 4 | 0 | Meter Overview: CELL1 *peak*, CELL2 *current*, CELL4 *rms*, CELL5 *current* |
| 4 | 1 | VU-Meter |
| 4 | 2 | leer |
| 4 | 3 | Preset-Browser |
| 4 | 4 | "no meter plugin selected" |
| 4 | 5 | Loudness |
| 4 | 6 | **noch nie gesehen:** drei senkrechte Balken, Peak/RMS-Skala links und rechts, 0 bis -25 |

## Die zwei Schlüsse

**1. Die Kopfzeile ist EIN Element mit acht Zellen, und jede Ebene verteilt sie
anders.** `0x011c` ist in L1, L2, L3 und L4 dasselbe Feld; nur die Firmware
entscheidet, wohin Zelle 1, 2, 3, 4, 5 gemalt wird. Damit ist die Frage nach
dem "grossen Textfeld" beantwortet: es gibt kein zweites, es ist unseres, nur
anders angeordnet. Wir benutzen heute ausschliesslich die L3-Anordnung.

**2. `L4 S6` existiert und wir kennen es nicht.** Wir fahren im Meter-View die
Screens 0 bis 5 (`uf1MeterScreenBurst_`). Screen **6** ist ein weiterer
Meter-Screen mit drei senkrechten Balken und einer Skala von 0 bis -25 auf
beiden Seiten — nichts davon steht in irgendeiner Aufnahme im Korpus.

## Was noch offen ist

Welche Zelle zeichnet die **Balken** in L1 und welche die **grüne Linie**?
Mit allen Elementen gleichzeitig sieht man, DASS etwas zeichnet, nie WAS.
Dafür hat die Sonde jetzt **"Only element (0 = all)"**: 1 bis 8 schreibt genau
eines (`0x0121`, `0x0113`, `0x0118`, `0x012b`, `0x0009`, `0x000a`, `0x0015`,
`0x0016`), die Liste steht unter dem Feld.
