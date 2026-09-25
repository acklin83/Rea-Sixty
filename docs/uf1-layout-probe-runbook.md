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
Balken und wo", nicht "welche Farbe": eine Position laesst sich zaehlen, eine
Farbe nur benennen.

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

---

# Ergebnis der Element-Isolierung, 21.09.2026

Sonde mit angehaltenem Pacer, ein Element nach dem anderen, die anderen auf
null. Layout 1, Screen 0.

| # | Adresse | was es zeichnet |
|---|---|---|
| 1-3 | `0x0121`, `0x0113`, `0x0118` | nichts — aber siehe die Warnung unten |
| **4** | **`0x012b`** | **die vier Farbbalken.** Vier Bytes, ein Palettenindex je Balken |
| 5 | `0x0009` | Pegel auf dem kleinen Fader-Display |
| **6** | **`0x000a`** | **der Peak-Strich** auf demselben Display. War im Painter seit jeher "meaning unknown" |
| 7 | `0x0015` | Comp-GR, eine LED bei Wert `0x05` |
| 8 | `0x0016` | Gate-GR, zwei LEDs bei Wert `0x0a` |

⛔ **Der Nullbefund bei 1 bis 3 war wertlos**, weil das Muster falsch war:
`0, 1, 2, 3` sind in der 7-Segment-Kodierung `(SEG7 << 1) | Punkt` blank, ein
Dezimalpunkt und ein halbes Segment. Die Sonde schreibt dort jetzt
`FE 00 FE 00`; der Durchgang mit den neuen Werten steht in Layout 1 noch aus.

## ⛔ Die Balken haengen an Layout 1

In Layout 3, unserer Kanal-Ebene, zeichnet `0x012b` **nichts**. Am Geraet
geprueft. Die vier Farbbalken sind also eine **Eigenschaft der Ebene** und kein
Widget, das man dazuschalten kann.

⇨ Und das faellt genau auf die zwei Maler aus Abschnitt 8 der Studie:
**SPREAD gehoert auf Layout 1** (vier Kanaele, vier Farbbalken, CELL1 und CELL2
als Beschriftung), **STRIP bleibt auf Layout 3**, wo der EQ-Graph lebt.

## Offen geblieben

* **Das 2- und 4-stellige 7-Segment-Feld in Layout 2** reagiert auf **keines**
  unserer acht Elemente, weder auf S0 noch auf S3. Es wird also von einer
  Adresse gefuettert, die wir nicht kennen. Kandidaten waeren die Ein-Byte-
  Elemente, die der Init beschreibt und wir nie anfassen (`0x0110`, `0x011a`,
  `0x011f`, `0x0123`, `0x0129`) — ungeprueft.
* `0x0121`, `0x0113`, `0x0118` sind weiter unbenannt.

---

# Layout 1 nochmal, mit zweistufiger Umschaltung, 21.09.2026 nachmittags

Frank am Gerät, L1 S0, Fotos IMG_4675 (alle), IMG_4676 (1 bis 3), IMG_4677 (13).

## ✅ Layout 1 HAT eine V-Pot-Reihe

Der Nullbefund von vorher ist widerlegt. Mit allen Elementen (IMG_4675) zeigt
L1 S0 von oben nach unten:

1. **Soft-Key-Zeile** `0x0104`: SK1 bis SK4
2. **Zehnstelliges 7-Segment-Feld**, alle Stellen unbeleuchtet. Wir haben
   `0x0119` nicht beschrieben, es ist also die leere Zeitzeile.
3. Die Kopfzellen (CELL1 unter *channel*, CELL2 unter *soft key*), bei „alle"
   ausgeblendet, siehe unten
4. **Die vier Farbbalken** `0x012b` auf der Trennlinie, **je einer über einem
   V-Pot**
5. **C1 bis C4** `0x010b`, ein Textfeld pro V-Pot
6. **Die Wertzeilen** `0x010e` (VPOT1 ... TEST)
7. **Die V-Pot-Balken** `0x010f`

⛔ **Die Wertzeile passt in Layout 1 nicht.** Unsere 19-Zeichen-Zeile (11 Label,
8 Wert) läuft über die Spaltenbreite hinaus: der Wert von Pot *n* landet links
vor dem Label von Pot *n+1* („VPOT1 · TES VPOT2 · TES VPOT3 · TES VPOT4 · TES").
SSL schreibt `0x010e` im DAW-Layer leer (cap141) und benutzt `0x010b` für den
Text. Wer Layout 1 fährt, braucht eine eigene, schmalere Zeile.

## 1 bis 3: weiter nichts

`0x0121`, `0x0113`, `0x0118` mit `FE 00 FE 00`: kein „8." irgendwo (IMG_4676).
Jetzt mit einem Muster, das auf einem 7-Segment sichtbar wäre. Weiter unbenannt.

## 11 blendet CELL1 und CELL2 aus

Und bei 13 waren sie wieder da. Die Sonde setzt pro Runde nur `0x0110` (auf
`0f`) und `0x011a` (auf `02`) zurück; `0x011f` und `0x0123` blieben nach 11 auf
`FF`, `0x0129` ist ab Werk `FF`. **Also ist es `0x0110` oder `0x011a`.** Dafür
hat die Sonde jetzt 14 und 15, je eines allein.

## ⛔ 9, 10, 12, 13 waren nicht isoliert

Die Sonde hat die V-Pot-Reihe und die Soft-Key-Texte bei „nicht gewählt"
übersprungen statt gelöscht. Darum steht die ganze Reihe auch auf dem Foto von
1 bis 3. Was bei 13 zu sehen war, ist also 13 plus Reste von 9 und 12, und ob
Stil `0x04` anders aussieht als `0x01`, lässt sich daraus nicht sagen.
Behoben: nicht gewählt heisst jetzt leer (Indexbyte allein, Balken 0, Stil 03),
und 0x011f/0x0123/0x0129 gehen auf ihre Init-Werte zurück.

## Zweiter Durchgang, isoliert

| # | was zu sehen ist |
|---|---|
| 9 | nur Zeitfeld, CELL1, CELL2. **Keine Wertzeile, keine Segmentleiste** |
| 10 | SK1 bis SK4 |
| 12 | C1 bis C4, ohne SK1 bis SK4 |
| 15 | **CELL1 und CELL2 weg: `0x011a = FF` blendet die Kopfzellen aus** |

9 schreibt Stil `0x01`; mit „alle" setzt 13 danach `0x04`, und dann waren
Wertzeile und Segmentleiste da. Vermutung: in Layout 1 gibt erst Stil `0x04`
die Reihe frei. Test: Element 16 = 9 mit Stil `0x04`.

**Bestätigt (Frank, Element 16): mit Stil `0x04` sind Wertzeilen und
Segmentleiste da.** In Layout 1 gibt erst dieser Stil die Reihe frei. Das
„irgendwas oberhalb der V-Pots" bei 13 war die Segmentleiste im Stil `0x04`.

## Was Layout 1 damit hergibt

Farbbalken pro V-Pot, ein kurzer Text pro V-Pot (`0x010b`), Wertzeile und
Segmentleiste, alles gleichzeitig, plus SK1 bis SK4, Zeitfeld und zwei
Kopfzellen. Kein EQ-Graph (der lebt in Layout 3). Offen: welche Breite die
Wertzeile in Layout 1 verträgt.

## Breite in Layout 1, am Gerät gemessen

Lineal `A..S` (19 Zeichen), nur auf Pot 1, Stil `0x04`:

| Feld | sichtbar | Lücke | Lage |
|---|---|---|---|
| `0x010e` Wertzeile | **A bis N (14)** | keine | ganz über Pot 1 |
| `0x010b` Text pro Pot | **A bis H (8)** | keine | ganz über Pot 1 |

⛔ **Korrektur zu oben.** Die Wertzeile läuft NICHT in den Nachbarpot. Auf
IMG_4678 steht „VPOT1" links in der Spalte von Pot 1 und „TES" rechts in
derselben Spalte, nur dicht vor dem Label von Pot 2. Abgeschnitten wird am
rechten Rand der eigenen Spalte.

Die Zahl hängt an den Buchstaben: 14 Grossbuchstaben passen, von
„VPOT1 + Leerzeichen + TEST" (19 Zeichen, Wert ab Stelle 16) waren 18 zu sehen.
Das passt zu einer Kappung nach **Pixelbreite** (Proportionalschrift), nicht
nach Zeichenzahl. Nicht einzeln nachgemessen.

## Messdurchgang Layout 1 (Elemente 19 bis 26)

| # | Feld | Lineal | sichtbar | vgl. Grossbuchstaben |
|---|---|---|---|---|
| 19 | Wertzeile `0x010e` | a..z | **a bis r (18)** | A bis N (14) |
| 20 | Text pro Pot `0x010b` | a..z | **a bis h (8)** | A bis H (8) |
| 21 | Soft-Key `0x0104` | A..S | **A bis L (12)** | |
| 22 | Soft-Key `0x0104` | a..z | **a bis o (15)** | A bis L (12) |
| 23 | CELL1 `0x011c` | A..X | **A bis J (10)** | |

**Daraus:**
- Wertzeile und Soft-Key werden nach **Pixelbreite** gekappt (Kleinbuchstaben
  passen mehr). Die Zeichenzahl ist dort keine feste Grenze.
- `0x010b` kappt nach **Zeichenzahl: 8**, egal welche Buchstaben.
- Unsere Wertzeile „VPOT1 + 10 Leerzeichen + TEST" zeigt in Layout 1 nur
  „VPOT1      TES": das letzte Zeichen fällt über den Rand. `uf1ValueLine` ist
  auf Layout 3 zugeschnitten (11 + 8 = 19) und passt in Layout 1 nicht.

**Stile, V-Pot-Reihe in Layout 1:**

| Stil | Reihe |
|---|---|
| `0x01` | nichts (Element 9) |
| `0x02` | da |
| `0x03` | da |
| `0x04` | da (Element 16) |
| `0x08` | nichts |

⛔ **Korrektur zu oben:** nicht „nur mit `0x04`", sondern **nicht mit `0x01` und
nicht mit `0x08`**. Genau die zwei, die in Layout 3 Zeiger und Mitte-Füllung
sind.

**Nachtrag, CELL2 und Fotos (IMG_4679, IMG_4680):**

- **CELL2** (`0x011c` Zelle 2, rechts unter *soft key*): Lineal a..x zeigt nur
  **a bis e (5)**. CELL1 links: A bis J (10). Die rechte Zelle ist die schmale.
- **Stil `0x02`** (4679): Segmentleiste als **Füllung von links**, mit unseren
  Werten 0x14/0x32/0x50/0x64 sichtbar etwa ein Fünftel, halb, vier Fünftel,
  voll. Also dieselbe Skala 0 bis 100 wie in Layout 3.
- **Stil `0x03`** (4680): Wertzeilen da, **Segmentleiste weg**. Wie in Layout 3
  („Pot leer"), nur dass der Text stehen bleibt.
- **Stil `0x04`** (4678): Kästchenleiste, zwei helle Kästchen pro Pot.

⛔ **Das trifft genau unseren Kanalmaler.** `uf1VpotBar_` setzt für jeden
normalen Pot `0x01` (unipolar) oder `0x08` (bipolar), und das sind die zwei
Stile, bei denen die ganze Reihe in Layout 1 verschwindet. Wer Layout 1 fährt,
muss den Stil umstellen, sonst ist die V-Pot-Reihe leer.

---

# Layout 2, zweistufig, 21.09.2026 nachmittags

## ✅ `0x011b` ist das Zahlenfeld von Layout 2

| Element | Screen | Bytes | Anzeige |
|---|---|---|---|
| 27 | S0 | `7e 0c` | **01** (zweistellig) |
| 28 | S0 | `0c b6 9e cc` | **12** (zweistellig, die ersten zwei) |
| 28 | S3 | `0c b6 9e cc` | **1234** (vierstellig) |

Kodierung wie `0x0119`: ein Byte pro Stelle, `(SEG7[Ziffer] << 1) | Punkt`.
S0 bis S2 zeigen zwei Stellen, S3 vier. Frank: S2 blau, S3 grün.

- **Die Firmware hält den Wert**, bis er neu geschrieben wird, auch beim
  Zurückschalten auf Element 0. Wer das Feld leeren will, muss es leer
  schreiben.
- Das löst den offenen Punkt von oben („reagiert auf keines unserer acht
  Elemente"): `0x011b` war nie in der Liste. Die Adresse steht seit cap141 im
  Eintritts-Burst von SSL (`7e 0c` = SSLs „01", die Kanal- oder Seitennummer).
- Und es erklärt den 10.08.: in Layout 3 gibt es dieses Feld nicht, und Bytes
  darauf haben dort die Kanalansicht zerlegt.

## Farbbalken

**Keine in Layout 2**, jetzt mit zweistufiger Umschaltung bestätigt. `0x012b`
gehört zu Layout 1.

**V-Pot-Reihe in Layout 2: dieselbe Stilregel wie in Layout 1.** Element 9
(Stil `0x01`) ohne Reihe, Element 16 (Stil `0x04`) mit. Passt zu cap141, wo SSL
dort `0x04` fährt. `0x02`, `0x03`, `0x08` in Layout 2 nicht einzeln geprüft.
