# UF8-Side-Car in Rea-Sixty und ORC: Plan

Stand 29.10.2026, dritte Fassung, nach Franks Rückmeldung vom selben Tag neu durchdacht. Nichts
gebaut. Die früheren Fassungen liegen in der Git-Geschichte (`c64ba2ab` bis `1f0835a0`).

---

## 1. Das Bild in fünf Sätzen

1. Der UF8 bekommt **Side-Car-Modi**, so wie der UF1 sein RME-Side-Car hat: Modi, die die
   **ganze Fläche** übernehmen und mit REAPER-Spuren nichts zu tun haben.
2. Es gibt drei: **TotalMix, Hue, DynaMount.** Dieselben drei, gleich bedient, in ORC und in
   Rea-Sixty.
3. **OBS ist kein Modus**, sondern Soft-Keys (Szenen, Aufnahme, Pause, Kapitel), die in jedem
   Modus gehen. Das gibt es in Rea-Sixty schon; ORC bekommt es auch.
4. **REC/RME über TotalReaper bleibt, wie es ist.** Das ist ein REAPER-Modus (Spureingänge,
   Hardware Direct Monitoring), kein Side-Car.
5. ORC und Rea-Sixty geben sich UF1 **und** UF8 beim REAPER-Start und -Ende weiter, wie heute
   den UF1.

---

## 2. Was der Nutzer sieht

### 2.1 Die Modi

| Modus | Fader | V-Pots | SEL / CUT / SOLO | Scribble, Farbe, VU |
|---|---|---|---|---|
| **TotalMix** | 8 Kanäle der gewählten Reihe; Eingänge und Playbacks = Pegel in den gewählten Submix | Eingänge: Preamp-Gain; sonst Pan | wählen / Mute / Solo | Kanalname, dB, TotalMix-Farbe, TotalMix-Pegel |
| **Hue** | Helligkeit pro Lampe/Zone | Farbton (FLIP: Sättigung), Druck = Weiss | Fokus / an-aus / Solo-Licht | wie heute in Rea-Sixty |
| **DynaMount** | X (FLIP: Y), wird beim Loslassen gesendet | Drehung | (frei) | wie heute in Rea-Sixty |

Hue und DynaMount verhalten sich so, wie Frank sie heute aus Rea-Sixty kennt. Neu ist nur, dass
sie die ganze Fläche haben: Strips ohne Lampe oder Stativ sind dunkel statt REAPER-Spuren.

**Tasten im TotalMix-Side-Car am UF8** (Frank 29.10.: Nav-Kreuz wie am UF1):

| Taste | Funktion | wie am UF1 |
|---|---|---|
| Nav ▲ ▼ | Reihe wählen (Inputs, Playbacks, Outputs, FX), mit Umlauf | `RmeInput.cpp:291` |
| Nav ◄ ► | Submix wählen, mit Umlauf | `RmeInput.cpp:296` |
| Nav Mitte | TotalMix-Fenster zeigen/verbergen | `RmeInput.cpp:302` |
| Bank ◄ ► | die 8 Kanäle um 8 weiter | |
| Channel ◄ ► | um 1 weiter | |
| PLUG-IN | Kanalansicht (2.6) | Druck auf den Kanal-Encoder |
| Kanal-Encoder | Main Volume | Jog |

Das Nav-Kreuz ist am UF8 das Zoom-Kreuz (Tasten 0x7A-0x7E). Die Funktionen kommen aus denselben
Zeilen wie am UF1 (`stepRow`, `stepSubmix`, `toggleWindow`), nicht nachgebaut.

### 2.2 Hinein und heraus

| | ORC | Rea-Sixty |
|---|---|---|
| TotalMix | Layer 1 (auch Shift + Layer 1) | **Shift + Layer 1** |
| Hue | Layer 2 (auch Shift + Layer 2) | Shift + Layer 2 |
| DynaMount | Layer 3 (auch Shift + Layer 3) | Shift + Layer 3 |
| heraus | dieselbe Taste nochmal: zurück zu TotalMix | dieselbe Taste nochmal: zurück zu REAPER |

- Layer ohne Shift bleibt in Rea-Sixty der Layer-Wechsel. ORC hat keine Layer, darum reicht dort
  die Taste allein; Shift + Layer geht in ORC genauso, damit die Hand in beiden Programmen
  dasselbe tun kann.
- Shift ist in Rea-Sixty die Tastatur (Spiegel `setKeyboardShiftHeld`, `main.cpp:47825` ff.) oder
  eine Flächentaste mit `mod_shift` (am UF1 ab Werk SHIFT). Der UF8 hat ab Werk keine Shift-Taste.
  In ORC gibt es keinen Tastatur-Spiegel; dort zählt die Taste allein bzw. SHIFT am UF1.
- Dahinter stehen drei Builtins (`uf8_sidecar_totalmix`, `uf8_sidecar_hue`,
  `uf8_sidecar_dynamount`), frei belegbar wie jedes Builtin.
- Die heutigen `selection_mode_hue` und `selection_mode_dynamount` werden zu Namen für dieselben
  Builtins. Wer sie auf eine Taste gelegt hat, behält seine Belegung.
- **Bestehende Installationen:** die Werksbelegung erreicht nur neue oder zurückgesetzte
  Konfigurationen. Für alle anderen setzt eine Hochstufung der Bindungsdatei Shift + Layer 1-3,
  **nur wo der Shift-Platz leer ist** (heute fällt ein leerer Shift-Platz auf den Layer-Wechsel
  zurück, `empty-modifier-slot-falls-back-to-plain`). Franks `bindings.json`: Layer 1-3 haben nur
  einen Plain-Eintrag, Shift ist frei (gelesen 29.10.).
- Die Layer-LED des aktiven Side-Cars leuchtet in der Farbe des Modus.

### 2.3 Soft-Keys

**V-POT und 1-5 bleiben in beiden Programmen die Soft-Key-Seiten**, wie heute in Rea-Sixty. Auf
die Seiten legt man Bänke. Die Bankarten, die es für diese Modi braucht:

| Bankart | gibt es | Druck | lang |
|---|---|---|---|
| TotalMix-Tasten (Dim, Mono, Speaker B, Talkback …) | ja (Side-Car-Bänke in `orc.json`) | Aktion | |
| TotalMix-Snapshots | ja | laden | |
| Hue-Szenen | ja (`HueScenes`) | Szene | dynamisch |
| OBS-Szenen | ja (`ObsScenes`) | Szene | |
| OBS Aufnahme / Pause / Kapitel | ja (Builtins) | | |
| **DynaMount-Presets** | **neu**, Abschnitt 4 | Preset anfahren | Preset speichern |

ORC bringt ab Werk auf V-POT eine TotalMix-Bank, auf 1 Hue-Szenen, auf 2 DynaMount-Presets, auf 3
OBS. Rea-Sixty ändert seine Werksbelegung nicht.

### 2.4 UF1 und UF8 zusammen

- Jede Fläche geht für sich in ihr Side-Car.
- **Sind beide in TotalMix, sind sie ein Pult:** der UF1 ist Master (sein Fader, STRIP, EQ-Graph,
  Main auf dem Jog), der UF8 zeigt acht Kanäle daneben. SEL am UF8 wählt den Kanal, den der UF1
  zeigt. Wählt der UF1 einen Kanal ausserhalb der acht, springt der UF8 hin.
- In ORC ist der UF1 immer TotalMix, der UF8 wechselt zwischen den drei Modi.
- In Rea-Sixty bleibt alles, was der UF1 heute kann (eigenes RME-Side-Car, eigenes `uf1_hue`).

### 2.5 Was ORC-Nutzer, Rea-Sixty-Nutzer und Nutzer mit beidem haben

| | nur Rea-Sixty | nur ORC | beides |
|---|---|---|---|
| Hue-Side-Car am UF8 | ja | ja | ja |
| DynaMount-Side-Car am UF8 | ja | ja | ja |
| DynaMount-Presets | ja | ja | ja |
| OBS-Soft-Keys | ja | ja | ja |
| TotalMix-Side-Car am UF8 | nein | ja | ja |
| TotalMix-Side-Car am UF1 | nein (wie heute) | ja | ja |
| REC/RME über TotalReaper | ja | nein | ja, in REAPER |
| Windows, Linux | Rea-Sixty ja, ohne TotalMix-Side-Car | ORC nur Mac | |

### 2.6 EQ, Dynamics usw. am UF8: die Kanalansicht

Am UF1 öffnet der Druck auf den Kanal-Encoder die Kanaleinstellungen (STRIP): Seiten wie Input,
Low Cut, EQ 1-3, Dyn, Expander, AutoLevel, Output, je 4 V-Pots und 4 Schalter, dazu der EQ-Graph.
Am UF8 allein (ORC ohne UF1) geht das so:

| UF8 | in der Kanalansicht |
|---|---|
| hinein / heraus | **PLUG-IN**, nochmal PLUG-IN = zurück. In Rea-Sixty heisst PLUG-IN dasselbe: Plug-in bzw. Kanalzug der gewählten Spur auf die V-Pots |
| welcher Kanal | der gewählte (SEL). SEL auf einem anderen Strip wechselt den Kanal, die Seite bleibt |
| 8 V-Pots | **eine Seite des UF1-STRIP**: V-Pot 1-4 = die 4 Regler der Seite, V-Pot 5-8 = die 4 Schalter der Seite (Ring voll = an, Drehen oder Drücken schaltet) |
| Wertzeile unter jedem V-Pot | Name und Wert, z.B. `Gain     +24`, `Freq   2.4k`, `EQ        ON` |
| Soft-Keys oben 1-8 | **Seite wählen**, direkt: Input, Input 2, Low Cut, EQ 1, EQ 2, EQ 3, Dyn, Expander. Die übrigen (AutoLevel, Output, FX) mit Page ◄ ►. Lampe auf der aktiven Seite |
| Fader | bleiben die Pegel der 8 Kanäle, man mischt weiter |
| obere Scribble-Zeile | bleibt der Kanalname über jedem Fader |
| EQ-Graph | gibt es am UF8 nicht; Werte als Zahl |
| V-POT / 1-5 | ohne Wirkung, solange die Soft-Keys oben Seiten wählen (wie im UF8-Plug-in-Mode von Rea-Sixty) |

- **Seiten aus derselben Konfiguration wie am UF1** (`rme.json`, `strip`): keine zweite Belegung,
  und Seiten, die ein Kanal nicht hat, fallen weg wie am UF1.
- **Mit UF1:** die Kanalansicht am UF8 geht auch; beide zeigen denselben Kanal, die Seiten wählt
  jede Fläche für sich.
- **In Rea-Sixty** gleich, sobald es den TotalMix-Side-Car am UF8 dort gibt (Stufe 7).
- Weiterhin ohne UF1: Kanal-Encoder = Main Volume; Dim, Mono, Talkback, Snapshots über die
  Soft-Key-Bank; Submix wählen wie am UF1 (Ausgang wählen).

---

## 3. Was sich in Rea-Sixty ändert

| | heute | neu |
|---|---|---|
| Hue, DynaMount | `SelectionMode`: N Strips gehören dem Modus, der Rest zeigt Spuren | UF8-Side-Car: ganze Fläche |
| Hue-Aufnahmelicht | nach REAPER-Aufnahme | **bleibt so.** In ORC gibt es keins (Frank 29.10.) |
| Hinein | `selection_mode_*`, ab Werk keine Taste | Shift + Layer 1-3 (ab Werk und per Hochstufung, nur wo leer), alte Builtin-Namen gehen weiter |
| DynaMount-Presets | gibt es nicht | Bankart + Builtins + Tabelle in den Einstellungen |
| TotalMix am UF8 | über REC/RME (TotalReaper) | bleibt; dazu Side-Car nur mit ORC |
| Übergabe | UF1, ohne Quittung | UF1 + UF8, mit Quittung (Abschnitt 5) |
| Einstellungen Hue, OBS, DynaMount | ExtState | ExtState, unverändert; dazu ein Import-Knopf aus ORC |
| Zwei UF8 | Hue, DynaMount nur links | Side-Car nur links (wie Tims Regel) |

**Was Rea-Sixty-Nutzer verlieren:** die gemischte Ansicht, also zwei Stative links und
sechs Spuren rechts gleichzeitig. Wer Stative fährt und gleichzeitig Spuren braucht, wechselt mit
einem Druck. Dafür ist es **ein** Begriff (Side-Car) für UF1 und UF8 und für beide Programme,
statt zwei Begriffe, die fast dasselbe tun.

---

## 4. DynaMount-Presets (neu, in beiden Programmen)

- **Ein Preset ist eine Szene:** die Position (X, Y, Drehung) **aller** eingeschalteten Stative
  auf einmal. Beispiel: „Drums nah“, „Drums Raum“, „Gesang“. Acht Stück, mit Namen.
- **Stative im Preset über ihre IP-Adresse**, nicht über die Platznummer. Wer ein Stativ in der
  Liste verschiebt, schickt sonst die Position des einen an das andere.
- **Bedienung:** Bankart `DynaMountPresets` auf jeder Soft-Key-Seite. Kurz = anfahren, lang =
  aktuelle Positionen speichern. Die Taste des zuletzt angefahrenen Presets leuchtet (wie bei Hue
  die zuletzt gerufene Szene). Geht in jedem Modus, auch während REAPER-Spuren auf den Fadern
  liegen, und auch ohne den DynaMount-Side-Car.
- **Builtins:** `dynamount_preset_recall` und `dynamount_preset_store` mit Parameter 1-8, für
  jede andere Taste.
- **Anfahren:** wie heute, Geschwindigkeit 9. Weil die Stative nichts zurückmelden, wird das
  Preset zum neuen Stand, und die Fader im DynaMount-Side-Car springen dorthin.
- **Einstellungen:** Reiter DynaMount bekommt eine Preset-Tabelle (Name, Werte je Stativ, Speichern,
  Löschen).
- **Speicher:** eigener Schlüssel `dynamount_presets` (Rea-Sixty ExtState, ORC `dynamount.json`),
  Trennzeichen `;` und Tab, nie ein Zeilenumbruch (ExtState-Lehre vom 27.08.), Round-Trip-Test.

---

## 5. Übergabe zwischen ORC und Rea-Sixty

Heute legt Rea-Sixty beim REAPER-Start eine Marke mit seiner Prozessnummer in ORCs Ordner, ORC gibt
den UF1 frei, Rea-Sixty probiert alle 5 s, bis es ihn hat. Das reicht für den UF8 nicht:

- Rea-Sixty öffnet den UF8 heute nur einmal beim Start und **versucht es nie wieder**. Hält ORC
  ihn in dem Moment, bleibt er bei ORC.
- **DynaMount-Stative melden ihre Position nicht.** Haben zwei Programme verschiedene Zahlen,
  springt das Stativ beim ersten Dreh.

Neu, in einfachen Worten:
1. ORC hinterlegt beim Start, dass es läuft.
2. Rea-Sixty legt beim REAPER-Start die Marke wie heute. Läuft ORC, **wartet** es (höchstens 3 s),
   bis ORC „losgelassen“ meldet.
3. ORC schliesst UF1 und UF8, stoppt seine Verbindungen und meldet „losgelassen“, zusammen mit den
   Stativ-Positionen.
4. Rea-Sixty übernimmt die Positionen (nur für Stative mit derselben IP) und öffnet die Geräte.
5. Beim REAPER-Ende umgekehrt: Rea-Sixty gibt die Positionen zurück, ORC holt die Geräte.

Ohne ORC ändert sich für Rea-Sixty nichts.

⛔ **Reihenfolge der Releases:** ein altes Rea-Sixty mit dem neuen ORC bekäme den UF8 nie in
REAPER (es versucht ihn nur einmal). Darum kommt **Rea-Sixty mit der neuen Übergabe zuerst** raus,
ORC mit UF8 danach. ORC erkennt ein altes Rea-Sixty und meldet im Menü, dass die REAPER-Erweiterung
aktualisiert werden muss.

---

## 6. Einstellungen

Jedes Programm hat seine eigenen. Rea-Sixty im ExtState wie heute, ORC in seinem Ordner, im selben
Format. Ein **Import-Knopf** im jeweils anderen Programm übernimmt sie (sichtbar nur, wenn das
andere installiert ist). Der Hue-Schlüssel kommt mit, die Bridge muss nicht neu gekoppelt werden.

Warum kein gemeinsamer Speicher: er bricht, wenn ORC vor dem neuen Rea-Sixty installiert wird
(Lampen still weg), wenn die Versionen auseinanderliegen, bei einer späteren Windows-Store-Fassung
von ORC (Windows trennt deren Dateien ab) und bei portablem REAPER. Getrennt plus Import geht in
jedem Fall.

Ausnahme, wie heute: die **TotalMix-Einstellungen gehören ORC**. Rea-Sixty liest sie nur, darum
gibt es den TotalMix-Side-Car in Rea-Sixty nur mit ORC.

---

## 7. Bauen

### 7.1 Grundsatz: ein Code für jeden Modus

Was einen Modus auf der Fläche ausmacht (Malen, Eingang), existiert einmal, beide Programme fahren
es. Für Hue und DynaMount ist Rea-Sixty die Vorlage: die Zweige werden **wortwörtlich** aus
`main.cpp` herausgezogen, Rea-Sixty läuft darauf, Frank sieht in REAPER keinen Unterschied, dann
erst bekommt ORC sie. TotalMix für den UF8 ist neu und entsteht gleich als geteiltes Modul.

### 7.2 Bausteine

| Baustein | Inhalt | Woher |
|---|---|---|
| UF8 in ORC | `UF8Device`, `Protocol` (REAPER-frei, heute nicht in ORC) | vorhanden |
| `Uf8Frames` | Eingangsframes mit den Fader-Fixes vom Oktober | aus `onUf8InputUnit_` (`main.cpp:27217`) |
| UF8-Schlaf | `uf8Frames` in `SurfaceSleep.h` | aus `pushSleepBrightness_` (`main.cpp:6327`) |
| `Uf8SideCar` | wer die Fläche hält, Ein-/Ausgang, Cache-Invalidierung beim Wechsel | neu, nach dem Muster `Uf1ScreenOwner` (`main.cpp:37011`) |
| `Uf8HueFace` / `Uf8HueInput` | Hue auf dem UF8 | aus `main.cpp:42185`, `39428`, `27472`, `28429`, `28635`, `28119`, `28205` |
| `Uf8DynaFace` / `Uf8DynaInput` | DynaMount auf dem UF8 | aus `main.cpp:42055`, `39407`, `27454`, `28400`, `28624` |
| `RmeSelection` | Reihe, Kanal, Submix, Achterfenster; UF1 und UF8 lesen es | aus `input::State` (`RmeInput.h:49`) |
| `Uf8RmeFace` / `Uf8RmeInput` | TotalMix auf dem UF8 | neu, Regeln aus `RmeUf1` |
| `DynaMountPresets` | Presets, Bankart, Builtins, Speicher | neu, in `DynaMountManager` |
| ORC: Hue, DynaMount, OBS | Manager, Einstellungsreiter (AppKit), Import | Manager vorhanden, Reiter neu |

⛔ Vor dem Side-Car: auflisten, was der normale UF8-Pfad besitzt (Scribbles, Farbbalken,
Fadermotor, V-Pot-Ring, VU, Strip-LEDs, globale LEDs, Soft-Key-Reihe) und wer es beim Verlassen
zurückgibt. Viermal ist das beim Hue-Bau schiefgegangen.

### 7.3 Reihenfolge

| | Inhalt | Programm | Release |
|---|---|---|---|
| 1 | Übergabe mit Quittung, UF8-Retry | beide | **Rea-Sixty zuerst** |
| 2 | `Uf8Frames`, UF8-Schlaf herausziehen | Rea-Sixty | mit 1 |
| 3 | UF8-Side-Car in Rea-Sixty: Hue und DynaMount ziehen um (herausgezogen, ganze Fläche), Shift + Layer, Hochstufung | Rea-Sixty | |
| 4 | DynaMount-Presets | Rea-Sixty | mit 3 |
| 5 | UF8 in ORC, TotalMix-Side-Car (`RmeSelection`, `Uf8RmeFace`), Kanalansicht (PLUG-IN), UF1 + UF8 als ein Pult | ORC | |
| 6 | Hue, DynaMount, Presets, OBS-Bänke in ORC, Reiter, Import in beiden | beide | |
| 7 | TotalMix-Side-Car am UF8 auch in Rea-Sixty, mit Kanalansicht | Rea-Sixty | |
| 8 | Handbücher, ORC 2.0.0 + Rea-Sixty aus demselben Commit | beide | |

Rea-Sixty-Nutzer bekommen mit 1 bis 4 schon etwas (Side-Car, Presets), bevor ORC den UF8 kann.

---

## 8. Entschieden (Frank 29.10.)

- Hue und DynaMount werden in Rea-Sixty UF8-Side-Car, die gemischte Ansicht fällt weg.
- Eintritt in Rea-Sixty ab Werk **Shift + Layer 1-3** (statt lang drücken), in ORC Layer allein.
- TotalMix-Side-Car am UF8 in Rea-Sixty wie am UF1, also nur mit ORC.
- DynaMount-Preset = alle Stative zusammen, acht Stück.
- Kein Marken-Cue `dyna:`.
- Kanal-Encoder am UF8 im TotalMix-Side-Car = Main Volume.
- ORC 2.0.0.
- Hue-Aufnahmelicht: nur Rea-Sixty, wie heute. ORC bekommt keins.
- So vorgesehen: das Achterfenster folgt der UF1-Auswahl immer; die TotalMix-Soft-Keys am UF8
  zeigen dieselbe Bank wie der UF1 (beide Hälften auf einmal); ORCs Grundstellung ist TotalMix.

**Entschieden mit 2.6 (Frank 29.10., „so bauen“):** Kanalansicht über PLUG-IN, eine Seite auf den 8 V-Pots. Nav-Kreuz wie am UF1, Bank ◄ ► = 8er-Bänke.

~~Offen, neu mit Abschnitt 2.6:~~

| | Frage | Vorschlag |
|---|---|---|
| 1 | Kanalansicht am UF8 über PLUG-IN? | ja |
| 2 | Eine STRIP-Seite auf den 8 V-Pots (4 Regler + 4 Schalter), Seitenwahl auf den Soft-Keys oben? Die Alternative wären zwei Seiten nebeneinander (8 Regler), dann müssten die Schalter auf die Soft-Keys und die Seitenwahl auf Page ◄ ► | eine Seite |

---

## 9. Nicht in diesem Plan

OBS als Fader-Modus (Frank: man hat gewöhnlich eine Quelle). Zwei UF8 in ORC. UC1 in ORC.
Windows-ORC. Marken-Cues und REAPER-Aufnahmelicht in ORC. MCU und Bildschirm-Mixer in ORC.

---

## 10. Belege

Gelesen am 29.10. (`b7b6deb0`):
- ORC hat keinen UF8-Code (`extension/CMakeLists.txt:1088-1117`). `UF8Device`, `Protocol`, alle
  Hue-, OBS-, DynaMount-Manager ohne REAPER-Include.
- Hue und DynaMount sind `SelectionMode` (`main.cpp:2113`), betreten über
  `selection_mode_hue/_dynamount` (`:57766-57776`).
- Das UF1-Side-Car hat eine Tür: SHIFT + MODE, Startansicht, Builtin `uf1_rme`
  (`uf1SetSideCar_`, `main.cpp:1489-1497`). Es gibt es nur mit ORCs `rme.json`
  (`g_rmeAvailable`, `:924-926`).
- REC/RME = `SelectionMode::Rec`/`RecMon` mit eingeschaltetem RME (`recRmeActive_`, `:2199`).
- Shift: Tastatur-Spiegel `setKeyboardShiftHeld` (`main.cpp:47825` ff.) oder `mod_shift`; ab Werk nur am UF1 (`Bindings.cpp:1536`). PLUG-IN in Rea-Sixty: Plain `ssl_strip_mode_toggle`, Shift `uf8_plugin_mode_toggle` (`Bindings.cpp:1298-1324`).
- Übergabe ohne Quittung (`main.cpp:1823-1847`, ORC `Surface.cpp:405-443`), UF8-Startfehler ohne
  Retry.
- TotalMix-Builtins heute: Dim, Mono, Speaker B, Talkback, Mute FX, Ext In, DURec, `rme_fader_main`,
  `rme_ask_again`, `rme_show_window`. Reihe und Submix wählt das Nav-Kreuz, kein neues Builtin nötig.
- Windows-Store-Apps: neue Dateien unter `AppData` landen an einem privaten Ort (Microsoft,
  „Understanding how packaged desktop apps run on Windows“).

Nicht belegt: wie schnell ein freigegebener UF8 unter macOS für REAPER frei ist (darum 3 s); ob
TotalMix Pegel für alle Reihen schickt; ob die UF8-Soft-Keys oben eine eigene Beschriftung auf dem Display haben, die die Seitennamen zeigen kann (vor Stufe 5 in der Feldkarte `Protocol.h` nachsehen).
