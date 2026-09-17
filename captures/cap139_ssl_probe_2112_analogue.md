# cap139 — SSL-Sonde, Analogue, SSL 360 2.1.12 (2026-09-17)

**Was:** unser eigener Sondenlauf (`REASIXTY_SSL_PROBE=1`), KEIN USB-Capture. Frank
spielte `uf1_meter_probe.wav` (53 s) einmal durch, eine Spur mit SSL Meter, UF1 auf
Analogue. Zwei Plug-in-Instanzen, Quellports 61219 und 62097.

**Warum:** drei Schlüsse über die SSL-Seite stammten aus unserer eigenen dekodierten
Ausgabe, und die kann nicht zeigen, was unser Dekoder wegwirft. Die Sonde läuft VOR
`parseDatagram` auf den rohen Paketen. Ausgelöst durch Issue #8 von `sollapse`.

## 1. VuPpm(0) war nie weg — BEWIESEN, zweifach

Auf dem Analogue-Bildschirm: `typed=116 UNTYPED=116 types=1:116`. Auf jedes
TextVuPpm(1)-Paket kommt genau ein Paket **ohne Feld-2-Marke**, mit zwei Floats.
Deren Werte folgen dem Signal und decken sich mit cap98 (Juli, als der Typ noch
ausgeschrieben wurde): `-12.01` beim -30-dBFS-Ton, `-2.01` bei -20 dBFS, `3.00`
geklemmt bei -10 und 0 dBFS, `-36.00` in der Stille.

Und das Plug-in sagt es selbst. Seine Prepare-Nachricht:

    f1=2 f2='VuPpm Meter Data' f6=2 f7=1        <- KEIN f5

Jeder andere Strom trägt f5 (TextVuPpm 1, BarPeak 2, BarRms 3, TextPeak 4,
TextRms 5, Rta 8, TextRta 9, Lissajous 10, Loudness 25/26/27). Nur der mit dem
Wert 0 lässt das Feld weg, weil 0 der Protobuf-Standard ist.

⛔ `46ef899` („Meter Pro 1.3.7 streamt DataType 0 an niemanden mehr") ist damit
widerlegt. Der Beleg dafür war unser eigener Auszug, und der kann ein Paket ohne
Typfeld gar nicht enthalten. Die Nadel-Emulation und der 3-s-Peak-Hold-Ersatz
umgehen einen Fehler in unserem Parser.

## 2. Felder auf dem Standardwert fallen weg — BEWIESEN

Fünf Objekte erscheinen mit ZWEI Signaturen, einmal mit Feld 1 und einmal mit
leerem Rumpf:

    obj=92b79049de04eac5 (360SelectedView)  fields=1/1,  UND  fields=(empty body)
    obj=86a543491dee40b7                    fields=1/0,  UND  fields=(empty body)
    obj=54ecd560fed9a72b                    fields=1/0,  UND  fields=(empty body)
    obj=2643402e9151f97a                    fields=1/1,  UND  fields=(empty body)
    obj=0020cb9363d774fc                    fields=1/0,  UND  fields=(empty body)

Unsere Leser testen ein Literal-Byte an `pay[8]` (`0x09`/`0x0a`/`0x08`). Ein leerer
Rumpf passt auf keinen davon, also bleibt der gespeicherte Wert stehen. Betrifft
u.a. unsere eigene View-Eigenschaft: Overview (= 0) kommt als leerer Rumpf.

## 3. Die Prepare-Nachrichten beschreiben alles — und wir lesen sie nicht

Feldbelegung, an diesem Lauf gemessen: f1 Gültigkeit (2 = gültig), f2 Legende,
f3 Einheit, f4 Kanalformat, f5 DataType, f6 **Anzahl Werte**, f7 Overload-Modus,
f8/f9/f10 Bereichsanfang/-ende/Leerwert. Textvariante aussen: f2 Alarmschwelle,
f4 Nachkommastellen.

    'Lissajous Meter Data'  f5=10  f6=17113      <- die 17113 steht da einfach
    'Rta Meter Data'        f5=8   f6=31
    'BarPeak Meter Data'    f4=2   f5=2  f6=2  f7=4
    'VuPpm Meter Data'             f6=2  f7=1

f7=1 nur bei VuPpm, 4 bei den Balken: das ist der Overload-Modus, und er erklärt,
warum die roten Bits nur auf VuPpm je wahr werden.

### ⚠ Nebenbefund: unsere vier Loudness-Readouts sind falsch zugeordnet

`uf1BuildLoudnessReadouts_` verdrahtet 15/16/17/18 mit „Integrated / Short-Term /
True Peak Max / Short-TermMax". Das Plug-in deklariert:

    15 'Integrated Dial'  LKFS      20 'True Peak Max'  dBFS   f7=2
    16 'LRA Dial'         LK        21 'Short-Term'     LKFS
    17 'Integrated'       LKFS      23 'Momentary'      LKFS
    18 'Loudness Range'   LK        24 'Momentary Max'  LKFS   f7=2

Drei von vier Zellen zeigen sicher den falschen Wert (16 als Short-Term, 17 als
True Peak Max, 18 als Short-TermMax). Und das feste Stilbyte 0x02 auf True Peak
Max deckt sich mit f7=2 an den beiden Max-Strömen.

**Decode:** die Zeilen sind selbsterklärend; Prepares mit
`python3 -c` über den `prepare hex:`-Zeilen auspacken, Protobuf.
