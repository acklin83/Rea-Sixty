# ORC für den UF8: Plan

Stand 29.10.2026. Nichts gebaut. Auftrag Frank 29.10.: ORC fährt auch den UF8, mit derselben
Übergabe an Rea-Sixty wie beim UF1, und mit vier Modi: TotalMix (allein oder mit dem UF1, dann
UF1 = Master, UF8 = 8-Kanal-Extender), Hue, DynaMount, OBS. Modi wählbar über die Layer-Tasten
oder über V-POT und 1-5, einstellbar in ORC.

Grundlage ist der Code am 29.10. (`b7b6deb0`). Jede Zeilenangabe unten ist in dieser Sitzung
gelesen. Abschnitt 11 trennt Belegtes von Vorschlag.

---

## 1. Was heute steht

| | Stand | Beleg |
|---|---|---|
| ORC kennt den UF8 | **gar nicht.** Kein `UF8Device`, kein `Protocol.cpp` in `ORC_SHARED_SOURCES` | `extension/CMakeLists.txt:1088-1117` |
| `UF8Device` + `Protocol.cpp` | REAPER-frei, `open(unitIndex)`, `kFirstFound`, `countAttached()` (Tims Stufe 1) | `UF8Device.h:45-64`, `UF8Device.cpp:1-19` |
| `Uf8Units.h` | reine Rechnung 1 oder 2 UF8, Extender-Seite, Bankbreite, getestet | `Uf8Units.h`, `tests/test_uf8_units.cpp` |
| UF8-Maler | **kein REAPER-freier Maler.** Alles in `pushZonesForVisibleSlots` (`main.cpp:41019` ff.), mit ~40 REAPER-Aufrufen | |
| UF8-Eingang | `onUf8InputUnit_` (`main.cpp:27217`), darin die Frame-Zusammensetzung mit dem `31 60`-Präfix (`:27242-27261`), danach alles mit REAPER vermischt | |
| UF8-Schlaf | nur in `main.cpp` (`pushSleepBrightness_` `:6327`, `tickSleep_` fragt `GetPlayState`). `SurfaceSleep.h` hat nur `uf1Frames` | `SurfaceSleep.h:90` |
| TotalMix-Auswahl | `reasixty::rme::input::State` (row, sel[4], submix, strip, stripPage, vpotBank, skHalf …), **gehört dem UF1-Eingang** (`RmeInput.h:32` zieht `UF1Protocol.h`) | `RmeInput.h:49-81` |
| `RmeState` | flächenneutral, hat auch Pegel pro Kanal (`levelIn/levelPb/levelOut`) | `RmeState.h:170` |
| `SelectionMode` | `Norm, Rec, RecMon, Auto, Instance, InstanceCycle, DynaMount, Hue`. **Kein TotalMix, kein OBS** | `main.cpp:2113` |
| Hue / DynaMount / OBS Manager | REAPER-frei (`HueManager`, `HueClient`, `HueColor`, `DynaMount*`, `ObsManager`, `ObsProto`, `WsClient`, `Sha256`, `HttpClient`) | Includes gelesen |
| Deren Konfiguration | **REAPER-ExtState** `rea_sixty/hue_config`, `hue_key`, `obs_config`, `obs_key`, `dynamount_devices`, `dynamount_fill`, `dynamount_state` | `main.cpp:61451-61507`, `:46223-46234`, `SettingsScreen.cpp:23654-23953` |
| Hue/DynaMount auf dem UF8 | Eingabe-Zweige `main.cpp:27454/27472` (Fader), `28400/28429` (Touch), `28624/28635` (V-Pot), `28119` (Push, nur Hue), `28205/28213` (SEL/CUT/SOLO, nur Hue); Maler `42055` (Dyna), `42185` (Hue); Farbbalken `39407/39428`; Strip-Reservierung `stripToVisibleSlot` `2677/2699` | |
| OBS auf dem UF8 | **kein Modus.** Nur Bankart `ObsScenes` (`Bindings.h:748`) und die Builtins `obs_record_toggle`, `obs_record_pause`, `obs_chapter_marker`, `obs_scene_recall` (`main.cpp:57827-57881`) | |
| Übergabe UF1 | Marke `ORC/handover` mit REAPERs pid (`main.cpp:1823-1847`), ORC prüft 2×/s (`Surface.cpp:405-443`), schliesst UF1 und stoppt `RmeManager`. **Keine Quittung:** Rea-Sixty versucht den UF1 alle 5 s (`main.cpp:49435`), den TotalMix-Port alle 3 s (`RmeManager.cpp:511-521`) | |
| Startfehler UF8 | wird nie wiederholt (nur der UF1 hat Retry), siehe Memory `uc1-wedged-after-failed-open` | |
| UF1-Extender heute | umgekehrt zu Franks Wunsch: in Rea-Sixty ist der **UF1 der 9. Strip des UF8** (`main.cpp:2884-2893`, `g_uf1Extender`) | |

**Die Bedienelemente** (`Bindings.cpp:411-478`, LEDs `Protocol.cpp:689-771`):

| Taste | Id | LED |
|---|---|---|
| Layer 1/2/3 | 0x40-0x42 | 0x3F/0x3E/0x3D |
| Quick 1/2/3 | 0x43-0x45 | 0x3C-0x3A |
| V-POT, 1-5 (`VPotBank`, `SoftKey1Bank..5`) | 0x68, 0x69-0x6D | 0x5F, 0x5E-0x5A |
| Soft-Keys oben, 8 pro Strip | 0x18-0x1F | 0x1F − strip |
| SOLO/CUT/SEL | 0x20-0x37 | |
| FLIP, Bank ◄ ►, Page ◄ ►, Channel, Plug-in, Fine, Zoom-Kreuz, Nav | siehe Tabelle im Code | |
| Kanal-Encoder | Pot 0x08 | |
| Transport | **gibt es am UF8 nicht** (nur UF1 0x3A-0x3E) | |

---

## 2. Grundsätze (aus der ORC-Geschichte, nicht neu)

1. **Ein Maler, eine Eingabe.** ORC fährt den Code der Extension wortwörtlich. Was heute nur in
   `main.cpp` lebt (Hue- und DynaMount-Zweige, UF8-Frames, UF8-Schlaf), wird erst
   herausgezogen, Rea-Sixty läuft darauf, Frank sieht es in REAPER unverändert, **dann** linkt ORC
   es. Methode wie am 25.09.: Probe-TU, `-fsyntax-only`, Fehlerliste = Hülle, jede Zeile gegen das
   Original diffen. Nie aus dem Kopf nachbauen (Stufe 7, 25.09.: „nicht raten, messen!").
2. **Die Auswahl existiert einmal.** Reihe, Kanal, Submix, Fenster: ein Struct, beide Flächen
   lesen es. Sonst laufen UF1 und UF8 auseinander.
3. **Zustand beim Aufrufer, Frames in eine Senke, Cache ohne Funktionsstatics**, Test mit zwei
   absichtlichen Brüchen.
4. **Eine Konfigurationsquelle pro Sache.** Kein Wert mit zwei Schreibern.
5. **ORC ist ein eigenes Produkt:** keine Rea-Sixty-Bezüge, keine Hilfesätze, SSL-Farbnamen.

---

## 3. Übergabe, Version 2 (beide Geräte, mit Quittung)

Heute merkt Rea-Sixty nicht, wann ORC losgelassen hat, und probiert blind. Mit dem UF8 und vor
allem mit DynaMount reicht das nicht mehr: **DynaMount ist offen geregelt**, die Position kennt nur
der Sender. Laufen zwei Programme mit verschiedenen Zahlen, fährt das Stativ beim ersten Dreh
von der Stelle, wo das andere es hinterlassen hat, auf die eigene, alte Zahl.

**Vorschlag:**

| Schritt | wer | was |
|---|---|---|
| 1 | ORC | legt beim Start `ORC/orc.pid` an |
| 2 | Rea-Sixty, REAPER-Start | schreibt `handover` (pid) wie heute. Lebt ORC (`orc.pid`, `kill(pid,0)`): wartet auf die Quittung, höchstens 3 s, öffnet bis dahin **weder UF1 noch UF8** und startet keinen Manager. Lebt ORC nicht: sofort wie heute |
| 3 | ORC | schliesst UF1 **und UF8**, stoppt RME, Hue, OBS, DynaMount, schreibt den DynaMount-Stand in die Datei (Abschnitt 8), dann `handover.released` mit REAPERs pid |
| 4 | Rea-Sixty | sieht `released` mit der eigenen pid: lädt den DynaMount-Stand, öffnet Geräte, startet Manager |
| 5 | Rea-Sixty, Beenden | schreibt DynaMount-Stand, löscht `handover` (wie heute) |
| 6 | ORC | sieht die Marke weg (oder pid tot): löscht `released`, lädt Stand, öffnet, startet |

Nebenwirkungen: die Übernahme wird schneller (kein 5-s-Raster), der UF8 bekommt den Retry, den er
heute gar nicht hat, und die Menüzeile „wer hält den UF1“ wird „wer hält UF1 / UF8“.

⛔ Die Einstellung in Rea-Sixty heisst heute „Take the UF1 over from ORC“
(`SettingsScreen_General.cpp:673`, ExtState `uf1_take_from_orc`). Mit dem UF8 gilt sie für beide;
Name und Suchindex (`MixerWindow.cpp:208`) mitziehen. **Frage A** (Abschnitt 10).

---

## 4. Der UF8 in ORC: Fundament

| Baustein | Inhalt | Herkunft |
|---|---|---|
| Gerät | `UF8Device.cpp`, `Protocol.cpp` in `ORC_SHARED_SOURCES`; Init-Sequenz kommt mit | vorhanden |
| `src/Uf8Frames.{h,cpp}` | Zusammensetzen der Eingangsframes inkl. `31 60`-Präfix und geteilter USB-Frames, Fader-Glättung (Frames ohne Bit 7), Touch-Flanken | **herausgezogen** aus `onUf8InputUnit_` (`main.cpp:27217-27300` und die Fader-Fixes `9d5cf58a`, `88e320ff`, `0b2d8d8e`). ⛔ Genau diese Fixes darf ORC nicht nachbauen, sonst kommen die Spikes vom 25.10. zurück |
| `SurfaceSleep.h` | `uf8Frames(asleep, led, lcd)` neben `uf1Frames` | herausgezogen aus `pushSleepBrightness_` |
| Wake | ein Druck auf irgendeiner Fläche weckt **beide** (eine Uhr, `surfsleep::Clock`) | ORC-Surface |
| Leerlauf | UF8 ohne aktiven Modus: dunkel bis auf die Modustasten | neu, klein |
| Menüleiste | „UF8: ORC / REAPER / nicht angeschlossen“ | neu |

ORC öffnet **ein** UF8 (`kFirstFound`). Zwei UF8 in ORC: nicht in diesem Plan, `Uf8Units` hält
die Tür offen (Abschnitt 9).

---

## 5. TotalMix auf dem UF8

### 5.1 Erst die Auswahl aus dem UF1-Eingang lösen

`input::State` mischt zwei Dinge: **was ausgewählt ist** (row, sel, submix) und **wie der UF1 es
bedient** (strip, stripPage, vpotBank, skHalf, Akkus, PushQuiet). Neu:

- `src/RmeSelection.h`: `row`, `sel[kRowCount]`, `submix`, `window[kRowCount]` (erster
  sichtbarer Kanal der 8, neu). Rein, ctest.
- `input::State` hält eine Referenz darauf statt eigener Felder. UF1-Verhalten unverändert;
  Rea-Sixty (`g_rmeIn`, `main.cpp:969`) und ORC (`Surface.h:90`) zeigen auf dieselbe Instanz ihres
  Prozesses.
- ⛔ Vorher alle Leser und Schreiber von `sel`, `row`, `submix` greppen (beide Programme,
  `RmeFace`, `RmeBuiltins`, `RmeSoftKeys`). Jeder Treffer kommt in die Bauliste.

`docs/rme-strip-and-uf8-plan.md` Abschnitt 4.2 nennt noch `g_rmeRow/g_rmeSel/g_rmeSubmix`; die gibt
es nicht mehr. Wird beim Bau korrigiert.

### 5.2 Belegung

| UF8 | TotalMix, UF8 allein | TotalMix, mit UF1 |
|---|---|---|
| 8 Fader | Pegel der 8 Kanäle der Reihe; in Input/Playback **der Knoten in den gewählten Submix** | gleich |
| 8 V-Pots | Input-Reihe: **Preamp-Gain** (Frank 21.09.), sonst Pan in den Submix | gleich |
| V-Pot-Druck | Gain/Pan auf 0 bzw. Mitte (PushQuiet wie am UF1) | gleich |
| Scribble oben | Name (Rolle bei Ausgängen, wie UF1, `RmeNames`) | gleich |
| Scribble unten / Wertzeile | dB, beim Drehen der V-Pot-Wert | gleich |
| Farbbalken | TotalMix-Farbe über `colourMap` (Colours-Reiter) | gleich |
| VU | **TotalMix-Pegel** aus `RmeState::levelIn/Pb/Out` (eigene Idee, Abschnitt 7) | gleich |
| SEL | Kanal wählen | Kanal wählen, **der UF1 zeigt ihn** (Fader, STRIP, EQ-Graph) |
| CUT / SOLO | Mute / Solo | gleich |
| Bank ◄ ► | Fenster ± 8 | gleich |
| Channel ◄ ► (Page) | Fenster ± 1 | gleich |
| Kanal-Encoder | **Main Volume** (der Job des UF1-Jogs, wenn kein UF1 da ist) | nach Frage C |
| Layer 1-3 | Reihe Input / Playback / Output (wenn die Modustasten V-POT/1-5 sind, Abschnitt 6) | gleich |
| Soft-Keys oben (8) | **die RME-Soft-Key-Bank aus `orc.json`, beide Hälften auf einmal** (Taste 1-4 = Hälfte 1, 5-8 = Hälfte 2), Bank ◄ ► mit Shift | gleich, aber nach Frage D |
| FLIP | Fader und V-Pots tauschen (Gain auf den Motorfader) | gleich |
| Channel-Taste (0x51) | **STRIP auf dem UF8:** die 8 V-Pots zeigen den gewählten Kanal, zwei STRIP-Seiten aus `rme.json` nebeneinander (4 + 4 Pots), Page ◄ ► blättert | aus, der STRIP gehört dem UF1 |

### 5.3 UF1 Master, UF8 Extender

- Ein Kanal ist gewählt, für beide Flächen (`RmeSelection`). SEL am UF8 setzt ihn, der
  Kanal-Encoder am UF1 auch.
- **Das Fenster folgt der Auswahl:** wählt der UF1 einen Kanal ausserhalb der 8, springt das UF8
  hin (wie REAPERs Mixer-Folgen). Abschaltbar? **Frage E.**
- Reihe und Submix wählt, wer zuletzt gedrückt hat; beide zeigen dasselbe.
- UF1 bleibt in seinem SPREAD/STRIP wie heute. Der UF8 nimmt ihm **nichts** weg.
- Die Konfiguration „UF1 Extender“ von Rea-Sixty (UF1 = 9. Strip) ist eine andere Sache und
  bleibt REAPERs. In ORC gibt es keinen Schalter: ist der UF1 da, ist er Master.

### 5.4 Module

- `src/Uf8RmeFace.{h,cpp}`: Maler, bekommt `RmeState`, `RmeSelection`, Konfig, schreibt Frames in
  eine Senke, Cache beim Aufrufer. Schreib-FORM der Zellen aus `pushZonesForVisibleSlots`
  übernehmen (Feldkarte `Protocol.h` lesen, nicht herleiten; 8 Zeichen `kUf8ScribbleChars`).
- `src/Uf8RmeInput.{h,cpp}`: Eingang auf `Uf8Frames`-Ereignissen, schreibt über `RmeUf1`
  (flächenneutrale Regeln: Schritte, Adressen, Faderkurve). `RmeUf1` bekommt dabei **keinen**
  neuen Namen in diesem Plan (Umbenennen kostet Diff, bringt nichts).
- Tests: `test_uf8_rme_face` (kalt alles, unverändert nichts, eine Wertzeile nur dieser Strip,
  Fenster ± 8), `test_rme_selection` (Fenster folgt Auswahl, Reihenwechsel behält pro Reihe).

---

## 6. Modi wählen

**Überholt durch Abschnitt 14** (Konfliktprüfung 29.10.): Modi werden in beiden Programmen
über dieselben Builtins betreten, die Tasten sind Bindungen in `orc.json` bzw. `bindings.json`.
Ein eigenes `ModeKeys`-Modul entfällt. Zweimal dieselbe Modustaste = zurück zur Grundstellung,
wie in Rea-Sixty. Der Text unten bleibt als Werksbelegung für ORC gültig.

### 6.1 Die Tasten

Vier Modi, Layer 1-3 sind drei Tasten. **Vorschlag: V-POT und 1-5**, sechs Plätze:

| Taste | ab Werk |
|---|---|
| V-POT | TotalMix |
| 1 | Hue |
| 2 | DynaMount |
| 3 | OBS |
| 4 | leer |
| 5 | leer |

**Eigene Idee: ein Platz ist ein Modus MIT Startwerten.** Zwei TotalMix-Plätze können
verschiedene Dinge zeigen, z.B. „V-POT = Inputs in Phones 1“, „4 = Inputs in Phones 2“, „5 =
Ausgänge“. Ein Kopfhörermix pro Musiker auf einer Taste. Platz = `{mode, row, submix}` für
TotalMix, `{mode}` für die anderen. Darum lohnen sich sechs Tasten auch bei vier Modi.

Die Layer-Tasten sind dann innerhalb von TotalMix frei für die Reihen (5.2). Wer die Layer-Tasten
als Modustasten will (Franks Variante 1), bekommt drei Plätze, und die Reihen gehen auf
V-POT/1-3. Die Einstellung tauscht die beiden Gruppen komplett.

Lampen: Modustaste des aktiven Modus hell in der Modusfarbe, belegte gedimmt, leere dunkel (wie
die Side-Car-Lampen, drei Zustände, `RmeFace::keyLamps`).

Zweimal dieselbe Modustaste: nichts (kein Zurück zu „aus“, ORC hat kein Norm). **Frage F.**

### 6.2 Was der UF1 tut, wenn der UF8 in Hue/DynaMount/OBS ist

**Vorschlag: der UF1 bleibt immer TotalMix.** Er ist der Monitorcontroller, Main Volume auf dem
Jog darf nie hinter einem Lichtmodus verschwinden. Rea-Sixty hat zwar einen UF1-Hue-Modus
(`uf1PaintHue_`, `main.cpp:35839`), den nimmt ORC nicht mit. **Frage G.**

### 6.3 Modul

`src/ModeKeys.h`: rein. Eingang: Taste, Konfig (welche Gruppe, sechs bzw. drei Plätze). Ausgang:
neuer Modus + Startwerte, Lampenbild. ctest. ORC hält den aktiven Modus; Wechsel invalidiert den
Cache des Malers, der vorher malte (Hue-Lehre 27.08.: Flanke invalidiert beide Maler, sonst
bleibt der alte Text auf dem Glas).

⛔ Vor dem Bau die Checkliste `uf1-screen-owning-mode-checklist` auf den UF8 übertragen: was
besitzt der normale Pfad (Scribbles, Farbbalken, Fader-Motor, V-Pot-Ring, VU, Strip-LEDs,
globale LEDs, Soft-Key-Reihe), und wer gibt es beim Wechsel zurück.

---

## 7. Hue und DynaMount

### 7.1 Herausziehen, bevor ORC sie bekommt

Alles, was auf dem UF8 Hue und DynaMount ausmacht, steht in `main.cpp` als Zweig innerhalb
REAPER-Funktionen. Neu, **wortwörtlich herausgezogen**:

| Modul | aus |
|---|---|
| `src/Uf8HueFace` | Maler `main.cpp:42185-42292`, Farbbalken `:39428-39445` |
| `src/Uf8HueInput` | Fader `:27472`, Touch `:28429`, V-Pot `:28635`, Push `:28119`, SEL/CUT/SOLO `:28205-28215` |
| `src/Uf8DynaFace` | Maler `:42055-42178`, Farbbalken `:39407-39421` |
| `src/Uf8DynaInput` | Fader + Cache `:27454-27466`, Touch-Senden `:28400-28424`, V-Pot `:28624`, FLIP-Re-Park |

Rea-Sixty ruft danach diese Module an genau den Stellen, wo heute der Zweig steht. Frank sieht in
REAPER keinen Unterschied; erst danach linkt ORC sie.

In ORC gibt es keine Spuren: **alle 8 Strips** gehören dem Modus. Strips ohne Lampe bzw. Stativ
bleiben dunkel. `stripToVisibleSlot`-Reservierung (links/rechts füllen) braucht ORC nicht; die
Füllrichtung bleibt eine Rea-Sixty-Einstellung.

### 7.2 Was ORC mitnimmt und was nicht

| | ORC | Grund |
|---|---|---|
| Hue: Lampen/Zonen auf Fader, V-Pot, CUT, SOLO, SEL | ja | |
| Hue: Szenen auf den 8 Soft-Keys (`HueScenes`, kurz = Szene, lang = dynamisch) | ja | Bankart gibt es |
| Hue: Aufnahmelicht nach REAPERs Aufnahme | nein | `GetPlayState` |
| **Hue: Aufnahmelicht nach OBS-Aufnahme** | Vorschlag | eigene Idee: ORC weiss über `RecordStateChanged`, wann OBS aufnimmt. Gleiche Lampenlogik (`recordingStarted/Stopped`), anderer Auslöser. **Frage H** |
| Hue/OBS: Marken-Cues `hue:` / `obs:` | nein | REAPER-Marker |
| DynaMount: X/Y mit FLIP, R auf V-Pot, Senden beim Loslassen, Home/Kalibrieren | ja | |
| **DynaMount: Positionen speichern** (Soft-Keys = 8 Speicher, lang drücken = speichern) | Vorschlag | eigene Idee, neue Funktion. **Frage I** |

### 7.3 Bauhinweise

- `HttpClient` für Hue: `macos_http.mm` muss in ORC **ohne ARC** kompiliert werden
  (Extension `CMakeLists.txt:466-468`, ORC setzt `-fobjc-arc` auf eigene .mm, `:1145-1149`).
- Farben der Strips kommen aus `uf8::selPaletteRgb` (`Protocol.h:225`, `Protocol.cpp`), kommt mit
  Abschnitt 4.
- Kein Manager fasst einen Speicher an. Gespeichert wird im ORC-Lauf über `takeConfigDirty()`,
  wie in Rea-Sixty.

---

## 8. Konfiguration: wem gehört was

**Überholt durch Abschnitt 14.2:** Weg a macht Rea-Sixty von ORC abhängig. Neuer Vorschlag:
ein neutraler Ordner, den keins der beiden Programme besitzt.

Heute liegt Hue/OBS/DynaMount in REAPERs ExtState. ORC kann das nicht lesen, ohne REAPERs
INI-Datei anzufassen. Drei Wege:

| | was | Folge |
|---|---|---|
| **a** | wie `rme.json`: **ORC besitzt** `ORC/hue.json`, `obs.json`, `dynamount.json`. Ist ORC installiert, liest Rea-Sixty nur (Tabs in Rea-Sixty zeigen die Werte, Felder gesperrt mit einem Satz „wird in ORC eingestellt“). Ohne ORC (Windows, Linux, Mac ohne ORC) bleibt ExtState wie heute | eine Quelle pro Rechner. Einmal-Übernahme: Rea-Sixty schreibt seine ExtState-Werte beim ersten Sehen des ORC-Ordners in die Dateien, wenn es sie noch nicht gibt |
| b | beide schreiben dieselben Dateien | zwei Schreiber, laufen gleichzeitig, verlieren Änderungen. Nein |
| c | getrennt | Lampen zweimal einrichten, Stative zweimal kalibrieren. Nein |

**Vorschlag a.** Gleiche Regel wie seit 25.09. für den Side-Car.
Der **DynaMount-Stand** (`dynamount_state`, die Positionen) ist kein Einstellungswert, sondern
Laufzustand. Er geht immer über `ORC/dynamount_state.json`, solange ORC installiert ist, und wird
von dem Programm geschrieben, das die Stative gerade hält (Abschnitt 3). Damit gibt es nie zwei
Schreiber gleichzeitig.

Passwort OBS und Hue-Schlüssel liegen dann im Klartext in einer Datei im Benutzerordner, wie
heute in der REAPER-INI. Kein Rückschritt, aber erwähnt.

**Frage B.**

---

## 9. OBS auf dem UF8 (neu, gibt es auch in Rea-Sixty nicht)

Vorschlag für den Modus:

| UF8 | OBS |
|---|---|
| Soft-Keys oben | Szenen (`ObsScenes`, Lampe auf der Szene auf Sendung) |
| Layer 1-3 bzw. freie Tasten | Aufnahme, Pause, Kapitelmarke (die drei Builtins) |
| **8 Fader** | **eigene Idee: OBS-Audioquellen.** `GetInputList`, `SetInputVolume`, `SetInputMute`, Rückweg über `InputVolumeChanged` / `InputMuteStateChanged` |
| CUT | Mute der Quelle |
| VU | `InputVolumeMeters` (Ereignis mit hoher Rate, eigenes Abo-Bit `1 << 16`) |
| Scribble | Quellenname, dB |

Die Anfrage- und Ereignisnamen stehen so in der offiziellen Protokolldoku (obs-websocket
`docs/generated/protocol.md`, am 29.10. per Abruf gelesen). Was es dafür braucht: `ObsProto`
lernt die vier Anfragen und zwei Ereignisse, `ObsManager` hält eine Quellenliste wie die
Szenenliste, Abo-Maske 68 wird um Inputs (`1 << 3`, aus derselben Doku zu bestätigen) und Meter
erweitert. ⛔ `test_obs` deckt `WsClient`/`ObsManager` heute nicht ab; der Stub
`scratchpad/obs_stub.py` vom 06.09. gehört dazu.

Ohne die Fader-Idee ist der OBS-Modus nur Soft-Keys, und die gibt es schon als Bank. **Frage J:**
mit Audio-Fadern, oder OBS nur als Bank?

Und ob der OBS-Modus auch in Rea-Sixty kommt: **Frage K.**

---

## 10. Offene Fragen

| | Frage | mein Vorschlag |
|---|---|---|
| A | Übergabe v2 mit Quittung (Abschnitt 3), und gilt „Take over from ORC“ für beide Geräte gemeinsam? | ja, gemeinsam |
| B | Konfiguration Hue/OBS/DynaMount: ORC besitzt, Rea-Sixty liest (Weg a)? | a |
| C | Kanal-Encoder am UF8, wenn der UF1 da ist: weiter Main Volume (zwei Wege zum selben Wert) oder Submix-Pegel? | Main Volume, auch mit UF1 |
| D | Soft-Keys oben in TotalMix: dieselbe RME-Bank wie der UF1 (beide Hälften), oder eine eigene UF8-Bankliste? | dieselbe Bank, keine zweite Konfiguration |
| E | Fenster folgt der UF1-Auswahl: immer, oder abschaltbar? | immer |
| F | Zweimal dieselbe Modustaste: nichts, oder UF8 dunkel/aus? | nichts |
| G | UF1 bleibt immer TotalMix, auch wenn der UF8 Hue/DynaMount/OBS ist? | ja |
| H | Hue-Aufnahmelicht in ORC nach OBS-Aufnahme? | ja, Häkchen „Rec light follows OBS“ |
| I | DynaMount-Positionsspeicher auf den Soft-Keys? | ja, aber eigener Schritt nach dem Rest |
| J | OBS-Modus mit Audio-Fadern? | ja |
| K | Kommen TotalMix-auf-UF8 und OBS-auf-UF8 auch in Rea-Sixty (`SelectionMode::TotalMix`, `::Obs`)? Ohne heisst: mit REAPER-Start verliert der UF8 diese Modi, der UF1 behält seinen Side-Car | TotalMix ja (die Module sind dann ohnehin geteilt, Rea-Sixty braucht nur die sechs Stellen aus `rme-strip-and-uf8-plan.md` 4.4), OBS später |
| L | Modustasten ab Werk V-POT/1-5 (sechs Plätze, Platz = Modus + Startwerte) | ja |
| M | ORC-Version: 1.3.0 oder 2.0.0 („ORC kann jetzt auch den UF8“) | 2.0.0 |

---

## 11. Reihenfolge

Jede Stufe einzeln baubar, jede mit Test für die reinen Teile, Frank prüft nach jeder Stufe.
Die Stufen 1, 2, 3 und 7a ändern Rea-Sixty; dort gilt: **mit einem UF1, einem UF8 und ohne ORC
ändert sich nichts.**

| Stufe | Inhalt | Programme |
|---|---|---|
| 1 | Übergabe v2 (`orc.pid`, `released`, Warten, UF8-Retry, Menüzeile) | beide |
| 2 | `Uf8Frames` + `uf8Frames`-Schlaf herausziehen, Rea-Sixty läuft darauf | Rea-Sixty |
| 3 | `RmeSelection` aus `input::State`, Fenster-Feld; UF1 unverändert | beide |
| 4 | UF8 in ORC: Gerät, Frames, Schlaf, Leerlauf | ORC |
| 5 | `Uf8RmeFace` + `Uf8RmeInput`, TotalMix auf dem UF8 allein, STRIP auf dem UF8 | ORC |
| 6 | UF1 Master + UF8 Extender (gemeinsame Auswahl, Fenster folgt) | ORC |
| 7a | Hue- und DynaMount-Zweige aus `main.cpp` herausziehen, Rea-Sixty läuft darauf | Rea-Sixty |
| 7b | Konfiguration nach Frage B, Einmal-Übernahme, Rea-Sixty-Tabs lesend | beide |
| 7c | Hue + DynaMount in ORC, AppKit-Reiter „Hue“ und „DynaMount“ (die ImGui-Reiter gehen nicht mit, nur das Datenmodell) | ORC |
| 8 | `ModeKeys`, Reiter „UF8“ in ORC (Modustasten-Gruppe, sechs Plätze mit Startwerten, V-Pot-Rolle pro Reihe) | ORC |
| 9 | OBS-Modus nach Frage J | ORC |
| 10 | Nach Frage K: `SelectionMode::TotalMix` in Rea-Sixty auf den geteilten Modulen | Rea-Sixty |
| 11 | Ideen nach H und I | ORC |
| 12 | Handbuch ORC (Template braucht `data-btn` für jede UF8-Taste, die CI-Prüfung „Verify the ORC manual“ erweitern), Rea-Sixty-Handbuch nur die Übergabe-Einstellung, Release ORC + Rea-Sixty aus demselben Commit | beide |

Stufe 8 kann vor Stufe 7 kommen, wenn Frank TotalMix + Modustasten früher in der Hand haben will.

---

## 12. Was ich NICHT baue

- Zwei UF8 in ORC (Tims Fläche). `Uf8Units` lässt es zu, eigener Plan.
- UC1 in ORC.
- Windows-ORC (bleibt `docs/orc-mac-release-plan.md`, „Windows, später“).
- Hue/OBS-Marken-Cues und REAPER-Aufnahmelicht in ORC.
- UF1-Hue-Modus in ORC.
- MCU/DAW-Steuerung in ORC (Entscheid 25.09. gilt).
- Ein ImGui- oder Bildschirm-Mixer in ORC (Entscheid 25.09. gilt).

---

## 13. Belegt und nicht belegt

**Belegt in dieser Sitzung (Code gelesen):** alle Zeilenangaben oben; ORC hat keinen UF8-Code;
`UF8Device`, `Protocol.cpp`, `Hue*`, `Obs*`, `WsClient`, `Sha256`, `DynaMount*`, `HttpClient`
ohne REAPER-Include; Konfiguration in ExtState; Übergabe ohne Quittung; `input::State` gehört dem
UF1-Eingang; kein `SelectionMode::TotalMix`/`Obs`; obs-websocket-Namen `GetInputList`,
`SetInputVolume`, `SetInputMute`, `InputVolumeChanged`, `InputMuteStateChanged`,
`InputVolumeMeters` (Protokolldoku abgerufen).

**Nicht belegt:**
- Wie lange libusb unter macOS braucht, bis ein von ORC geschlossenes UF8 für REAPER frei ist
  (darum 3 s Obergrenze statt einer gemessenen Zahl).
- Ob TotalMix Pegel für alle drei Reihen schickt, wenn „Send Peak Level“ an ist (`RmeState.h:167`
  sagt: nur bei Änderung und nur mit dieser Einstellung). Vor Stufe 5 an Franks TotalMix
  mitlesen (`ORC_TRACE=1`, nur lesen).
- Das Abo-Bit für Input-Ereignisse in obs-websocket (`1 << 3`) aus dem Gedächtnis, vor Stufe 9 in
  der Doku nachsehen.
- Ob `slotLabelForVisibleSlot` (`main.cpp:39556`) überhaupt aufgerufen wird; die Suche fand keinen
  Aufrufer. Beim Herausziehen in 7a klären, toten Code nicht mitnehmen.

---

## 14. Konfliktprüfung (Frank 29.10.)

Zwei Bedingungen: **(1) Die Modi fühlen sich in ORC und Rea-Sixty gleich an. (2) ORC ist keine
Abhängigkeit für Rea-Sixty.** Der Plan oben gegen beide gelesen, dazu der Code.

### 14.1 Funde

| # | Stelle im Plan | Konflikt mit | Befund | Auflösung |
|---|---|---|---|---|
| 1 | 8a: ORC besitzt hue/obs/dynamount.json, Rea-Sixty liest nur, wenn ORC installiert ist | (2) | Rea-Sixtys Einstellungen wären gesperrt, sobald ORC daliegt. Das ist eine Abhängigkeit, nur eine weiche | neutraler Ordner, 14.2 |
| 2 | **heute schon:** der TotalMix-Side-Car in Rea-Sixty | (2) | `g_rmeAvailable` (`main.cpp:924-926`): ohne ORCs `rme.json` gibt es keinen Side-Car, unter Windows/Linux gar keinen. Entscheid vom 25.09. | **Frage O** |
| 3 | Frage K: TotalMix auf dem UF8 auch in Rea-Sixty | (2) | erbt Fund 2 | mit O |
| 4 | 3: DynaMount-Stand in `ORC/dynamount_state.json` | (2) | Ordner gehört ORC | neutraler Ordner |
| 5 | 3: Übergabe v2 | (2) | Rea-Sixty wartet nur, wenn `orc.pid` lebt. Ohne ORC wie heute | kein Konflikt |
| 6 | 6.1/6.3: eigenes `ModeKeys`-Modul in ORC | (1) | Rea-Sixty betritt Modi über `selection_mode_hue` / `selection_mode_dynamount` (`main.cpp:57766-57776`, Umschalter `registerSelectionModeToggle` `:57671`). Ein zweiter Mechanismus fühlt sich anders an und läuft auseinander | ORC benutzt dieselben Builtins über seine Bindungs-Engine (läuft dort schon, `orc.json`). Neue Builtins `selection_mode_obs`, `selection_mode_totalmix` (Parameter = Startwerte) entstehen einmal, für beide |
| 7 | 6.1 Frage F: zweimal dieselbe Taste tut nichts | (1) | in Rea-Sixty geht derselbe Modus-Builtin zurück auf Norm | gleiches Verhalten: zurück zur Grundstellung. In ORC ist die Grundstellung einstellbar (ab Werk TotalMix, ohne TotalMix-Link dunkel) |
| 8 | 6.1: V-POT/1-5 als Modustasten | (1) | in Rea-Sixty sind das die Soft-Key-Seiten (`softkey_bank_select`, `Bindings.cpp:1408-1428`). Rea-Sixty ab Werk umbelegen nimmt bestehenden Nutzern die Seiten | nur ORCs Werksbelegung. Wer es in REAPER gleich will, bindet dieselben Builtins auf dieselben Tasten. **Innerhalb** des Modus ist alles gleich, nur der Eingang ist ab Werk verschieden. **Frage P** |
| 9 | 7.1: ORC belegt alle 8 Strips, „Füllrichtung braucht ORC nicht“ | (1) | in Rea-Sixty sitzen Lampe 1 bzw. Stativ 1 je nach Füllrichtung links oder rechts (`stripToVisibleSlot` `:2677/2699`, `mountForStrip`) | ORC nimmt dieselbe Füllrichtung aus derselben Konfiguration. Lampe 3 sitzt in beiden Programmen auf demselben Strip. Die übrigen Strips: Spuren in REAPER, dunkel in ORC (unvermeidlich) |
| 10 | 5.2/7.2/9: Soft-Keys zeigen im Modus die Szenen | (1) | in Rea-Sixty ändert ein Modus die Soft-Key-Reihe NICHT (kein Soft-Key-Zweig unter den 16 Hue-Stellen) | Option „Ein Modus bringt seine Bank mit“ im geteilten Bindungscode, in beiden Programmen gleich. Rea-Sixty ab Werk aus (heutiges Verhalten), ORC ab Werk an. **Frage N** |
| 11 | 6.2: UF1 bleibt in ORC immer TotalMix | (1) | in Rea-Sixty ist der UF1-Hue-Modus ein **eigenes** Builtin `uf1_hue` (`main.cpp:58558`), er folgt dem UF8 nicht | kein Konflikt: in beiden folgt der UF1 dem UF8-Modus nicht. ORC bietet `uf1_hue` in der ersten Runde nicht an (Maler `uf1PaintHue_` müsste dafür auch heraus) |
| 12 | 7.2 Frage H: Aufnahmelicht nach OBS nur in ORC | (1) | Rea-Sixty hat OBS auch | Auslöser „REAPER-Aufnahme / OBS-Aufnahme“ als Feld in der geteilten Hue-Konfiguration, in beiden. ORC zeigt nur OBS |
| 13 | 7.2 Frage I: DynaMount-Positionsspeicher | (1) | nur in ORC gedacht | im geteilten DynaMount-Modul, also in beiden, oder gar nicht |
| 14 | 9: OBS-Modus nur ORC (Frage K: Rea-Sixty später) | (1) | Ein Modus, den es nur in einem Programm gibt, fühlt sich nicht gleich an | als geteiltes Modul, `SelectionMode::Obs` in Rea-Sixty in derselben Stufe |
| 15 | 7.2: Marken-Cues `hue:` / `obs:` | (1) | nur REAPER hat Marker | bleibt Rea-Sixty, ist keine Bedienung der Fläche |
| 16 | 7.3: zwei Einstellungsoberflächen (ImGui, AppKit) für dieselben Werte | (1) | doppelter Oberflächencode, Feldnamen können auseinanderlaufen | unvermeidlich (ImGui geht in ORC nicht mit, Entscheid 25.09.). Gleiche Feldnamen, gleiche Reihenfolge, gleiche Grenzen aus dem Modul (nicht im Fenster) |
| 17 | 5.2: Kanal-Encoder = Main Volume in TotalMix | (1) | nur relevant, wenn TotalMix-auf-UF8 auch in Rea-Sixty kommt | im geteilten Modul, gilt dann in beiden |

### 14.2 Die Antwort auf „separat oder Rea-Sixty ändern“

**Weder getrennt noch Rea-Sixty umbauen. Ein Modus, ein Code, eine Konfiguration, und Rea-Sixty
ist die Vorlage.**

- **Verhalten:** Rea-Sixtys Hue und DynaMount sind am Gerät erprobt. ORC übernimmt sie, wie sie
  sind (Abschnitt 7.1, wortwörtlich herausgezogen). Rea-Sixtys Bedienung ändert sich dadurch
  nicht. Getrennt bauen hiesse zwei Maler, zwei Eingaben, und die laufen auseinander: genau die
  Fehlerklasse, die ORCs ersten Maler am 25.09. gekostet hat.
- **Neues entsteht einmal, im geteilten Modul, und kommt in beide:** OBS-Modus, TotalMix auf dem
  UF8, Positionsspeicher, Aufnahmelicht nach OBS. Was in REAPER keinen Sinn hat, bekommt
  REAPER nicht; was in ORC keinen Sinn hat (Marker, REAPER-Aufnahme), bekommt ORC nicht.
- **Was sich an Rea-Sixty ändert, ist nur unter der Haube:** Code wandert in Module, und die
  Konfiguration zieht aus dem ExtState in Dateien.
- **Getrennte Konfiguration** (Lampen zweimal einrichten, Stative zweimal kalibrieren) bricht
  Bedingung (1) im Kleinen: Lampe 3 wäre in ORC eine andere als in REAPER, sobald einer
  nachzieht und der andere nicht.

**Konfiguration, neuer Vorschlag:** ein neutraler Ordner, der weder ORC noch Rea-Sixty gehört.

| | Mac | Windows | Linux |
|---|---|---|---|
| Ordner | `~/Library/Application Support/Stoersender/` | `%APPDATA%\Stoersender\` | `~/.config/stoersender/` |
| Dateien | `hue.json`, `obs.json`, `dynamount.json`, `dynamount_state.json` | gleich | gleich |

- Rea-Sixty benutzt den Ordner auf **allen** Plattformen, mit oder ohne ORC. Ein Codeweg, keine
  Abhängigkeit. Beim ersten Start mit der neuen Version übernimmt es die ExtState-Werte einmal
  in die Dateien, wenn es sie noch nicht gibt, und lässt den ExtState danach liegen (Rückweg bei
  einem Downgrade).
- ORC benutzt denselben Ordner.
- ⛔ **Damit gibt es zwei Schreiber**, entgegen der Regel „ein Wert, ein Schreiber“. Begründung,
  warum es hier trotzdem geht: Die Manager laufen dank Übergabe nie in beiden Programmen
  zugleich, und der einzige Schreiber, der nicht von einer Hand kommt, ist die Bridge-Suche
  (`HueManager.cpp:944`), also ein Manager. Was bleibt, sind Eingaben in zwei Einstellungsfenstern.
  Darum: Schreiben nur bei Änderung, atomar (Zwischendatei + `rename`), jede Seite liest bei
  geänderter mtime neu (1×/s, wie heute `rme.json`) und schreibt immer Lesen-Ändern-Schreiben
  der ganzen Datei. Verloren geht etwas nur, wenn Frank in derselben Sekunde in beiden Fenstern
  dasselbe Feld ändert.
- Klartext-Passwörter (OBS, Hue-Schlüssel) wie heute im ExtState, nur in einer anderen Datei.
- Setup-Bundle (`SetupBundle.cpp`) trägt diese Werte heute nicht (`kKeys` ohne hue/obs/dyna), der
  Kommentar `main.cpp:61484` geht davon aus. Beim Umzug klären, Kommentar richtigstellen.

### 14.3 Die neue Frage O: der TotalMix-Side-Car

Bedingung (2) gilt nach dem Wortlaut auch für ihn, und heute ist sie verletzt: Rea-Sixty bietet
den Side-Car nur an, wenn ORCs `rme.json` daliegt (Entscheid 25.09., „Die ganze Konfig für den
RME Side-Car kommt in ORC“). Zwei Möglichkeiten:

| | was | Folge |
|---|---|---|
| **O1** | so lassen: TotalMix ist ORCs Sache, Rea-Sixty leiht es nur | Abhängigkeit bleibt, aber nur für TotalMix. TotalMix-auf-UF8 in Rea-Sixty (Frage K) gibt es dann auch nur mit ORC. Windows/Linux ohne Side-Car |
| O2 | `rme.json` und die RME-Bänke (heute in `orc.json`) ziehen in denselben neutralen Ordner | Side-Car geht ohne ORC, auch unter Windows/Linux. Aber: Rea-Sixty hat seit 25.09. keine RME-Einstellungen mehr (Seite Modes, RME entfernt). Ohne ORC gälte nur die Werksbelegung, oder Rea-Sixty bekommt die Seite zurück |

Vorschlag: **O1**, und die Bedingung (2) für Hue, OBS und DynaMount gilt ab jetzt. Grund: den
Side-Car gibt es in Rea-Sixty für ORC-Nutzer, DynaMount (seit Juni) und Hue (seit August) gibt
es in Rea-Sixty ohne ORC, die dürfen nichts verlieren. O2 lohnt sich erst, wenn jemand den Side-Car unter Windows
will.

### 14.4 Neue offene Fragen

| | Frage | Vorschlag |
|---|---|---|
| N | Option „Modus bringt seine Soft-Key-Bank mit“ (Rea-Sixty ab Werk aus, ORC an)? | ja |
| O | TotalMix-Side-Car bleibt an ORC gebunden (O1) oder zieht in den neutralen Ordner (O2)? | O1 |
| P | Modus-Builtins in Rea-Sixty ab Werk auf keine Taste (wie heute), in ORC auf V-POT/1-5? | ja |
| Q | Neutraler Ordner `Stoersender` als Name? Er steht dann auf Kundenrechnern | ja, oder `Rea-Sixty and ORC` |

Frage B ist damit durch 14.2 ersetzt, F durch Fund 7, G durch Fund 11 bestätigt. H, I, J, K
gelten weiter, aber immer für beide Programme.

### 14.5 Folgen für die Reihenfolge (Abschnitt 11)

- 7b wird: Konfiguration in den neutralen Ordner, **zuerst in Rea-Sixty allein** (mit
  Einmal-Übernahme aus dem ExtState), ein Release, dann ORC liest mit.
- 8 wird: die Modus-Builtins (`selection_mode_obs`, `selection_mode_totalmix` mit Startwerten) im
  geteilten Code, Werksbelegung in ORC, Option aus Frage N. Kein `ModeKeys.h`.
- 9 (OBS-Modus) und 10 (TotalMix in Rea-Sixty) bauen jeweils beide Programme in derselben Stufe.

---

## 15. Einwand Frank 29.10.: der gemeinsame Ordner muss bei jedem Kunden funktionieren

Frank: „Das Programm geht an viele User raus. Die haben nur Rea-Sixty, nur ORC oder beides.
Muss immer funktionieren.“ Den Vorschlag aus 14.2 (gemeinsamer Ordner `Stoersender`) gegen alle
drei Gruppen und gegen die Reihenfolge der Installation geprüft. **Er hält nicht.**

### 15.1 Wo der gemeinsame Ordner bricht

| # | Fall | Was passiert |
|---|---|---|
| 1 | Kunde hat Rea-Sixty mit eingerichtetem Hue, installiert **zuerst** das neue ORC, aktualisiert Rea-Sixty später | ORC legt `hue.json` an. Rea-Sixty übernimmt seinen ExtState nur, „wenn es die Datei noch nicht gibt“: seine Lampen sind still weg. Genau der Fehler, den der ExtState-Newline im August hatte |
| 2 | ORC neuer als Rea-Sixty (getrennte Releases, ReaPack gegen DMG) | ORC schreibt ein Format, das das ältere Rea-Sixty nicht kennt. Liest es halb, schreibt es beim nächsten Speichern das alte Format zurück und löscht ORCs neue Felder |
| 3 | ORC unter Windows als Store-MSIX (der Weg aus `orc-mac-release-plan.md`, „Windows, später“) | Microsoft: neu angelegte Dateien unter `AppData` landen bei einer virtualisierten App an einem privaten Ort pro App. Legt ORC die Datei an, sieht Rea-Sixty sie nie (Quelle: learn.microsoft.com, „Understanding how packaged desktop apps run on Windows“, Abschnitt AppData, am 29.10. gelesen). Legt Rea-Sixty sie zuerst an, geht es. Also wieder abhängig von der Reihenfolge |
| 4 | Portables REAPER oder zwei REAPER-Installationen | heute hat jede ihren eigenen ExtState. Ein Ordner pro Benutzer macht daraus still eine gemeinsame Konfiguration |
| 5 | Zwei Schreiber | in 14.2 als tragbar begründet, bleibt aber eine Ausnahme von der Regel |

Fall 1 bis 3 treffen genau die Kunden, die Frank meint: wer beide Programme hat, hat sie selten
am selben Tag installiert und selten auf demselben Stand.

### 15.2 Neuer Vorschlag: jedes Programm besitzt seine Einstellungen, geteilt wird der Code

| | Rea-Sixty | ORC |
|---|---|---|
| Hue / OBS / DynaMount-Einstellungen | bleiben im ExtState, **wie heute**, nichts zieht um | eigene Dateien im ORC-Ordner (`hue.json`, `obs.json`, `dynamount.json`) |
| Format | dasselbe: die `serialize`/`deserialize` der Manager (`HueManager.cpp:696`, `ObsManager.cpp:103`, `DynaMountManager.cpp:194`), nur der Speicherort ist verschieden | |
| Bedienung | der geteilte Modus-Code (Abschnitt 7.1, 14.1) | derselbe |
| Übernehmen | Knopf „Import from ORC“ in den Reitern Hue/OBS/DynaMount, nur sichtbar, wenn ORCs Ordner existiert | Knopf „Import from Rea-Sixty“ (bzw. neutral benannt, Produktregel), nur sichtbar, wenn REAPERs `reaper-extstate.ini` existiert. Liest nur die drei Schlüssel, schreibt nie in REAPERs Dateien |

Damit gilt für jede Gruppe:

| Gruppe | funktioniert, weil |
|---|---|
| nur Rea-Sixty | nichts ändert sich, kein neuer Ordner, kein neuer Pfad |
| nur ORC | ORC liest und schreibt nur seinen eigenen Ordner |
| beides, egal in welcher Reihenfolge installiert | jedes läuft mit seinen eigenen Einstellungen. Der Import ist ein Knopfdruck, kein Automatismus, also nie ein stiller Verlust |
| beides, verschiedene Stände | jedes liest nur sein eigenes Format. Der Import überspringt unbekannte Felder |
| Windows-ORC als MSIX (später) | ORC schreibt nur in seinen eigenen Bereich. Der Import liest nur (Lesen fremder Dateien unter `AppData` ist erlaubt, laut derselben Quelle) |
| portables REAPER | unverändert |

Der Hue-Schlüssel kommt beim Import mit, die Bridge muss also nicht neu gekoppelt werden.

**Preis:** wer beide hat und in einem Programm eine Lampe umstellt, hat sie im anderen nicht
umgestellt, bis er importiert. Die BEDIENUNG ist trotzdem gleich (Bedingung 1 betrifft den Code,
nicht die Belegung). Rea-Sixty ändert sich gar nicht, ausser dem Import-Knopf.

### 15.3 Was trotzdem zwischen den Programmen wandern muss: der DynaMount-Stand

Positionen sind Laufzustand, keine Einstellung, und offen geregelt (Abschnitt 3). Sie wandern
**nur mit der Übergabe**, also nur wenn beide installiert sind und ORC läuft:
- ORC schreibt die Positionen in `handover.released` (ORC-Ordner, existiert dann sicher).
- Rea-Sixty schreibt sie beim Beenden in `handover.returned` im ORC-Ordner, aber nur, wenn es den
  Ordner gibt (so wie heute schon die Marke, `orcHandoverMark_`, `main.cpp:1823-1847`).
- Ohne ORC: Rea-Sixty hält seinen `dynamount_state` im ExtState wie heute.
- Die Positionen werden nur übernommen, wenn die Stativ-IP in beiden Einstellungen dieselbe ist.
  Sonst gilt der eigene Stand (zwei verschieden eingerichtete Programme dürfen sich nicht
  gegenseitig Stative zuordnen).

### 15.4 Alt gegen neu bei der Übergabe

| Rea-Sixty | ORC | Ergebnis |
|---|---|---|
| alt (v1-Marke, wartet nicht) | neu | UF1 wie heute. **UF8 bricht:** das alte Rea-Sixty öffnet den UF8 beim REAPER-Start genau einmal und wiederholt nie (Memory `uc1-wedged-after-failed-open`). ORC hält ihn in dem Moment noch (es merkt die Marke erst nach bis zu 500 ms), also landet der UF8 bei diesem Rea-Sixty nie in REAPER, bei keinem Start. Nicht reparierbar, das alte Rea-Sixty ist draussen. Darum: **Rea-Sixty mit Übergabe v2 und UF8-Retry wird ein Release VOR ORC mit UF8.** ORC erkennt die alte Marke (keine Versionszeile) und zeigt im Menü „REAPER cannot take the UF8 yet: update the REAPER extension“ (kein Produktname, ORC-Regel), statt still festzuhalten |
| neu | alt (kein `orc.pid`) | Rea-Sixty sieht kein laufendes ORC, wartet nicht, verhält sich wie heute |
| neu | neu | Übergabe v2 |
| neu | nicht installiert | wie heute, kein Ordner, keine Marke |

### 15.5 Was damit aus Abschnitt 14 wird

- 14.2, „neutraler Ordner“: **verworfen.** Frage Q entfällt.
- 14.5, Stufe 7b: wird „Import-Knöpfe in beiden, ORCs eigene Dateien“. Kein Umzug in Rea-Sixty,
  keine Einmal-Übernahme.
- Frage O (TotalMix-Side-Car) bleibt offen; O2 („neutraler Ordner“) fällt aus demselben Grund
  weg. Die Alternative wäre ein eigener `rme`-Speicher in Rea-Sixty mit Import, also derselbe Weg
  wie hier. Vorschlag bleibt O1.
