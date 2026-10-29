# ORC für den UF8: Plan

Stand 29.10.2026, zusammengeführte Fassung nach zwei Prüfrunden mit Frank. Nichts gebaut.

**Auftrag (Frank 29.10.):** ORC fährt auch den UF8, mit derselben Übergabe an Rea-Sixty wie beim
UF1, sodass beide Geräte mit und ohne REAPER gehen. Modi: TotalMix (allein oder mit dem UF1, dann
UF1 = Master, UF8 = 8-Kanal-Extender), Hue, DynaMount, OBS. Wählbar über Layer 1-3 oder über
V-POT und 1-5, einstellbar in ORC.

**Bedingungen (Frank 29.10., zweite und dritte Runde):**
1. Die Modi fühlen sich in ORC und Rea-Sixty gleich an.
2. ORC ist keine Abhängigkeit für Rea-Sixty.
3. Es muss bei jedem Kunden funktionieren: nur Rea-Sixty, nur ORC, oder beides, in jeder
   Installationsreihenfolge und auf jedem Versionsstand.

Grundlage ist der Code am 29.10. (`b7b6deb0`). Zeilenangaben sind in der Sitzung gelesen.
Abschnitt 13 trennt Belegtes von nicht Belegtem.

---

## 1. Was heute steht

| | Stand | Beleg |
|---|---|---|
| ORC kennt den UF8 | **gar nicht.** Kein `UF8Device`, kein `Protocol.cpp` in `ORC_SHARED_SOURCES` | `extension/CMakeLists.txt:1088-1117` |
| `UF8Device` + `Protocol.cpp` | REAPER-frei, `open(unitIndex)`, `kFirstFound`, `countAttached()` | `UF8Device.h:45-64`, `UF8Device.cpp:1-19` |
| `Uf8Units.h` | reine Rechnung für 1 oder 2 UF8, getestet | `tests/test_uf8_units.cpp` |
| UF8-Maler | **kein REAPER-freier Maler.** Alles in `pushZonesForVisibleSlots` (`main.cpp:41019` ff.) mit ~40 REAPER-Aufrufen | |
| UF8-Eingang | `onUf8InputUnit_` (`main.cpp:27217`), Frame-Zusammensetzung mit `31 60`-Präfix (`:27242-27261`), danach mit REAPER vermischt | |
| UF8-Schlaf | nur `main.cpp` (`pushSleepBrightness_` `:6327`, `tickSleep_` fragt `GetPlayState`). `SurfaceSleep.h` kennt nur den UF1 | `SurfaceSleep.h:90` |
| TotalMix-Auswahl | `reasixty::rme::input::State` (row, sel, submix, strip, stripPage, vpotBank, skHalf …), **gehört dem UF1-Eingang** (`RmeInput.h:32` zieht `UF1Protocol.h`) | `RmeInput.h:49-81` |
| `RmeState` | flächenneutral, mit Pegeln pro Kanal (`levelIn/levelPb/levelOut`) | `RmeState.h:170` |
| `SelectionMode` | `Norm, Rec, RecMon, Auto, Instance, InstanceCycle, DynaMount, Hue`. Kein TotalMix, kein OBS | `main.cpp:2113` |
| Modus betreten | Builtins `selection_mode_hue`, `selection_mode_dynamount` über `registerSelectionModeToggle`; nochmal drücken = zurück auf Norm. Ab Werk auf keiner Taste | `main.cpp:57671-57776` |
| UF1-Hue | eigenes Builtin `uf1_hue`, folgt dem UF8-Modus nicht | `main.cpp:58558` |
| Hue / DynaMount / OBS Manager | REAPER-frei (`HueManager`, `HueClient`, `HueColor`, `DynaMount*`, `ObsManager`, `ObsProto`, `WsClient`, `Sha256`, `HttpClient`) | Includes gelesen |
| Deren Einstellungen | REAPER-ExtState `rea_sixty/hue_config`, `hue_key`, `obs_config`, `obs_key`, `dynamount_devices`, `dynamount_fill`, `dynamount_state` | `main.cpp:61451-61507`, `:46223-46234` |
| Hue/DynaMount am UF8 | Zweige: Fader `main.cpp:27454/27472`, Touch `28400/28429`, V-Pot `28624/28635`, Push `28119` (nur Hue), SEL/CUT/SOLO `28205/28213` (nur Hue), Maler `42055/42185`, Farbbalken `39407/39428`, Strip-Reservierung `2677/2699` | |
| Soft-Keys im Modus | ein Modus ändert die Soft-Key-Reihe in Rea-Sixty **nicht** | keine Soft-Key-Stelle unter den 16 Hue-Treffern |
| OBS am UF8 | **kein Modus.** Bankart `ObsScenes` (`Bindings.h:748`), Builtins `obs_record_toggle`, `obs_record_pause`, `obs_chapter_marker`, `obs_scene_recall` | `main.cpp:57827-57881` |
| Übergabe UF1 | Marke `ORC/handover` mit REAPERs pid (`main.cpp:1823-1847`), ORC prüft 2×/s (`Surface.cpp:405-443`). **Keine Quittung:** Rea-Sixty versucht den UF1 alle 5 s (`main.cpp:49435`), den TotalMix-Port alle 3 s (`RmeManager.cpp:511-521`) | |
| UF8-Startfehler | wird nie wiederholt (nur der UF1 hat Retry) | Memory `uc1-wedged-after-failed-open` |
| TotalMix-Side-Car in Rea-Sixty | nur wenn ORCs `rme.json` daliegt (`g_rmeAvailable`). Die einzige bestehende Abhängigkeit von ORC | `main.cpp:924-926` |
| „UF1 Extender“ in Rea-Sixty | UF1 = 9. Strip des UF8, also umgekehrt zu ORC-Wunsch | `main.cpp:2884-2893` |

**Bedienelemente** (`Bindings.cpp:411-478`, LEDs `Protocol.cpp:689-771`):

| Taste | Id | LED |
|---|---|---|
| Layer 1/2/3 | 0x40-0x42 | 0x3F/0x3E/0x3D |
| Quick 1/2/3 | 0x43-0x45 | 0x3C-0x3A |
| V-POT, 1-5 (`VPotBank`, `SoftKey1Bank..5`) | 0x68, 0x69-0x6D | 0x5F, 0x5E-0x5A |
| Soft-Keys oben, eine pro Strip | 0x18-0x1F | 0x1F minus Strip |
| SOLO/CUT/SEL | 0x20-0x37 | |
| Kanal-Encoder | Pot 0x08 | |
| Transport | gibt es am UF8 nicht | |

In Rea-Sixty sind V-POT und 1-5 die Soft-Key-Seiten (`softkey_bank_select`,
`Bindings.cpp:1408-1428`).

---

## 2. Grundsätze

1. **Ein Modus, ein Code.** Was einen Modus auf der Fläche ausmacht (Maler, Eingang, Regeln),
   existiert einmal und beide Programme fahren es. Rea-Sixty ist die Vorlage: Hue und DynaMount
   werden wortwörtlich aus `main.cpp` herausgezogen (Probe-TU, `-fsyntax-only`, Fehlerliste =
   Hülle, jede Zeile gegen das Original diffen). Rea-Sixty läuft zuerst darauf, Frank sieht es in
   REAPER unverändert, **dann** linkt ORC es. Nie nachbauen.
2. **Neues entsteht einmal und kommt in beide** (OBS-Modus, TotalMix am UF8, Ideen aus Abschnitt
   10). Nur was in einem Programm keinen Sinn hat, bleibt draussen (Marker und REAPER-Aufnahme in
   ORC).
3. **Strip-Elemente gehören dem Modus, globale Tasten sind Bindungen.** Fader, V-Pots, Scribbles,
   Farbbalken, VU, SEL/CUT/SOLO verhalten sich in beiden Programmen gleich, weil derselbe Code sie
   fährt. Alles, was eine globale Taste in einem Modus tut (Modus wählen, Reihe wählen, Aufnahme),
   ist ein **Builtin**, in beiden Programmen gleich. Nur die Werksbelegung darf sich
   unterscheiden.
4. **Jedes Programm besitzt seine Einstellungen.** Rea-Sixty im ExtState wie heute, ORC in
   seinem Ordner. Kein gemeinsamer Ordner, kein zweiter Schreiber. Ausgetauscht wird per Knopf
   (Import) oder über die Übergabe (Laufzustand).
5. **Die Auswahl existiert einmal pro Prozess.** Reihe, Kanal, Submix, Fenster: ein Struct, UF1
   und UF8 lesen es.
6. **Zustand beim Aufrufer, Frames in eine Senke, Cache ohne Funktionsstatics**, Test mit zwei
   absichtlichen Brüchen.
7. **ORC ist ein eigenes Produkt:** keine Rea-Sixty-Bezüge im Fenster und Menü, keine
   Hilfesätze, SSL-Farbnamen.

---

## 3. Übergabe, Version 2 (beide Geräte, mit Quittung)

Heute merkt Rea-Sixty nicht, wann ORC losgelassen hat. Mit DynaMount reicht das nicht: die
Stative sind **offen geregelt**, die Position kennt nur der Sender. Laufen zwei Programme mit
verschiedenen Zahlen, springt das Stativ beim ersten Dreh.

| Schritt | wer | was |
|---|---|---|
| 1 | ORC | legt beim Start `ORC/orc.pid` an |
| 2 | Rea-Sixty, REAPER-Start | schreibt `handover` (pid + Versionszeile). Lebt ORC (`orc.pid`, `kill(pid,0)`): wartet auf die Quittung, höchstens 3 s, öffnet bis dahin weder UF1 noch UF8 und startet keinen Manager. Lebt ORC nicht oder gibt es den Ordner nicht: sofort wie heute |
| 3 | ORC | schliesst UF1 und UF8, stoppt RME, Hue, OBS, DynaMount, schreibt `handover.released` mit REAPERs pid **und den Stativ-Positionen** |
| 4 | Rea-Sixty | sieht `released` mit der eigenen pid: übernimmt Positionen (nur für Stative mit gleicher IP in seinen eigenen Einstellungen), öffnet Geräte, startet Manager |
| 5 | Rea-Sixty, Beenden | schreibt seine Positionen in `handover.returned` (nur wenn der ORC-Ordner existiert, wie heute die Marke), löscht `handover` |
| 6 | ORC | sieht die Marke weg oder die pid tot: übernimmt `returned` (gleiche IP-Regel), öffnet, startet |

Ohne ORC hält Rea-Sixty seinen `dynamount_state` im ExtState wie heute.

Nebenwirkungen: Übernahme schneller (kein 5-s-Raster), der UF8 bekommt einen Retry, die Menüzeile
in ORC zeigt „UF1: … / UF8: …“ (ORC, REAPER, nicht angeschlossen).

**Alt gegen neu:**

| Rea-Sixty | ORC | Ergebnis |
|---|---|---|
| neu | nicht installiert | wie heute, kein Ordner, keine Marke |
| neu | alt (kein `orc.pid`) | Rea-Sixty wartet nicht, wie heute |
| neu | neu | Übergabe v2 |
| alt | neu | UF1 wie heute. **Der UF8 kommt nie in REAPER:** das alte Rea-Sixty öffnet ihn beim Start einmal, ORC hält ihn in dem Moment noch, und es wird nie wiederholt. Im alten Rea-Sixty nicht reparierbar. Darum **Rea-Sixty mit Übergabe v2 und UF8-Retry ein Release vor ORC mit UF8.** ORC erkennt die alte Marke (keine Versionszeile) und meldet im Menü „REAPER cannot take the UF8 yet: update the REAPER extension“ |

Die Einstellung „Take the UF1 over from ORC“ (`SettingsScreen_General.cpp:673`, ExtState
`uf1_take_from_orc`) gilt dann für beide Geräte; Name und Suchindex (`MixerWindow.cpp:208`)
mitziehen. **Frage A.**

---

## 4. Der UF8 in ORC: Fundament

| Baustein | Inhalt | Herkunft |
|---|---|---|
| Gerät | `UF8Device.cpp`, `Protocol.cpp` in `ORC_SHARED_SOURCES` | vorhanden |
| `src/Uf8Frames` | Eingangsframes inkl. `31 60`-Präfix und geteilter USB-Frames, Fader-Glättung (ohne Bit 7), Touch-Flanken | **herausgezogen** aus `onUf8InputUnit_` samt den Fader-Fixes `9d5cf58a`, `88e320ff`, `0b2d8d8e`. Nachgebaut kämen die Spikes vom 25.10. zurück |
| `SurfaceSleep.h` | `uf8Frames(asleep, led, lcd)` neben `uf1Frames` | herausgezogen aus `pushSleepBrightness_` |
| Wecken | ein Druck auf einer Fläche weckt beide (eine `surfsleep::Clock`) | ORC |
| Grundstellung | UF8 ohne Modus: dunkel bis auf die Modustasten | neu |

ORC öffnet ein UF8 (`kFirstFound`). Zwei UF8 in ORC sind nicht Teil dieses Plans.

---

## 5. Modi wählen

- **Builtins in beiden Programmen:** `selection_mode_hue`, `selection_mode_dynamount` (gibt es),
  neu `selection_mode_obs` und `selection_mode_totalmix`. Verhalten wie heute in Rea-Sixty:
  derselbe Builtin nochmal = zurück zur Grundstellung. In Rea-Sixty ist das Norm (Spuren), in ORC
  die einstellbare Grundstellung (ab Werk TotalMix, ohne TotalMix-Verbindung dunkel).
- **Eigene Idee: ein Modus mit Startwerten.** `selection_mode_totalmix` nimmt einen Parameter
  (Reihe, Submix). Zwei Tasten können so zwei Kopfhörermixe sein: „Inputs in Phones 1“, „Inputs in
  Phones 2“. Darum lohnen sich sechs Tasten auch bei vier Modi.
- **Werksbelegung ORC:** V-POT = TotalMix, 1 = Hue, 2 = DynaMount, 3 = OBS, 4 und 5 frei. Wer
  lieber die Layer-Tasten will, legt die Builtins in ORCs Tastenreiter auf Layer 1-3 (Franks
  Variante 1); die Reihen wandern dann auf V-POT/1-3. ORCs Einstellungsfenster bekommt dafür einen
  Schalter, der beide Gruppen auf einmal umlegt.
- **Werksbelegung Rea-Sixty:** unverändert, die Modus-Builtins liegen auf keiner Taste, V-POT/1-5
  bleiben Soft-Key-Seiten. Wer es wie in ORC will, belegt dieselben Tasten selbst. **Frage B.**
- **Lampen:** aktiver Modus hell, belegt gedimmt, leer dunkel. Kommt aus dem Toggle-Zustand der
  Builtins, also in beiden Programmen gleich.
- **Wechsel:** jeder Modus-Wechsel invalidiert den Cache des Malers, der vorher malte (Hue-Lehre
  27.08.). Vor dem Bau die Checkliste `uf1-screen-owning-mode-checklist` für den UF8 schreiben:
  was besitzt der normale Pfad (Scribbles, Farbbalken, Motor, V-Pot-Ring, VU, Strip-LEDs, globale
  LEDs, Soft-Key-Reihe), und wer gibt es beim Wechsel zurück.
- **Soft-Key-Reihe im Modus:** neue Option im geteilten Bindungscode „Ein Modus bringt seine Bank
  mit“ (Hue: Szenen, OBS: Szenen, TotalMix: RME-Bank, DynaMount: Positionsspeicher falls gebaut).
  Rea-Sixty ab Werk aus (heutiges Verhalten), ORC ab Werk an. **Frage C.**
- **UF1:** folgt dem UF8-Modus in beiden Programmen nicht. In ORC bleibt er immer TotalMix: Main
  Volume auf dem Jog darf nie hinter einem Lichtmodus verschwinden. `uf1_hue` bietet ORC in der
  ersten Runde nicht an.

---

## 6. TotalMix auf dem UF8

### 6.1 Erst die Auswahl aus dem UF1-Eingang lösen

- `src/RmeSelection.h`: `row`, `sel[kRowCount]`, `submix`, `window[kRowCount]` (erster
  sichtbarer der 8 Kanäle, neu). Rein, ctest.
- `input::State` hält eine Referenz darauf statt eigener Felder. UF1-Verhalten unverändert.
- Vorher alle Leser und Schreiber von `sel`, `row`, `submix` greppen (`RmeInput`, `RmeFace`,
  `RmeBuiltins`, `RmeSoftKeys`, `main.cpp`, `orc/`). Jeder Treffer kommt in die Bauliste.
- `docs/rme-strip-and-uf8-plan.md` 4.2 nennt noch `g_rmeRow/g_rmeSel/g_rmeSubmix`; die gibt es
  nicht mehr. Beim Bau korrigieren.

### 6.2 Belegung

| UF8 | TotalMix, UF8 allein | mit UF1 |
|---|---|---|
| 8 Fader | Pegel der 8 Kanäle; Input/Playback = **Knoten in den gewählten Submix** | gleich |
| 8 V-Pots | Input-Reihe **Preamp-Gain** (Frank 21.09.), sonst Pan in den Submix | gleich |
| V-Pot-Druck | Gain 0 bzw. Pan Mitte | gleich |
| Scribble | Name (Rolle bei Ausgängen, `RmeNames`), Wertzeile dB bzw. V-Pot-Wert, 8 Zeichen | gleich |
| Farbbalken | TotalMix-Farbe über `colourMap` | gleich |
| VU | **TotalMix-Pegel** aus `RmeState` (eigene Idee) | gleich |
| SEL | Kanal wählen | Kanal wählen, **der UF1 zeigt ihn** (Fader, STRIP, EQ-Graph) |
| CUT / SOLO | Mute / Solo | gleich |
| Bank ◄ ► / Channel ◄ ► | Fenster ± 8 / ± 1 | gleich |
| Kanal-Encoder | **Main Volume** | **Frage D** |
| Reihe wählen | neue Builtins `rme_row_input/playback/output/fx` (heute gibt es nur Kontrollraum, DURec, `rme_fader_main` u.a.), ORC ab Werk auf Layer 1-3 (+ FX auf Shift) | gleich |
| Soft-Keys oben | RME-Soft-Key-Bank aus `orc.json`, beide Hälften auf einmal (1-4, 5-8) | **Frage E** |
| FLIP | Fader und V-Pots tauschen | gleich |
| Channel-Taste (0x51) | **STRIP auf dem UF8** (eigene Idee): die 8 V-Pots zeigen den gewählten Kanal, zwei STRIP-Seiten aus `rme.json` nebeneinander, Page ◄ ► blättert | aus, der STRIP gehört dem UF1 |

### 6.3 UF1 Master, UF8 Extender

- Ein gewählter Kanal für beide Flächen (`RmeSelection`). SEL am UF8 und Kanal-Encoder am UF1
  setzen denselben.
- **Das Fenster folgt der Auswahl:** wählt der UF1 einen Kanal ausserhalb der 8, springt der UF8
  hin. **Frage F.**
- Reihe und Submix: wer zuletzt gedrückt hat, beide zeigen dasselbe.
- Der UF1 behält SPREAD/STRIP wie heute, der UF8 nimmt ihm nichts weg.
- Rea-Sixtys „UF1 Extender“ (UF1 als 9. Strip) bleibt REAPERs Sache. In ORC ist der UF1 immer
  Master, ohne Schalter.

### 6.4 Module

- `src/Uf8RmeFace`: Maler, bekommt `RmeState`, `RmeSelection`, Konfiguration, Frames in eine
  Senke, Cache beim Aufrufer. Schreib-Form der Zellen aus `pushZonesForVisibleSlots` übernehmen
  (Feldkarte `Protocol.h`, `kUf8ScribbleChars` = 8).
- `src/Uf8RmeInput`: Eingang auf `Uf8Frames`-Ereignissen, Regeln aus `RmeUf1` (Schritte,
  Adressen, Faderkurve). Kein Umbenennen von `RmeUf1`.
- Tests: `test_uf8_rme_face` (kalt alles, unverändert nichts, eine Wertzeile nur dieser Strip,
  Fenster ± 8), `test_rme_selection` (Fenster folgt Auswahl, Reihenwechsel merkt pro Reihe).

### 6.5 TotalMix am UF8 auch in Rea-Sixty?

Bedingung 1 sagt ja. Mit den geteilten Modulen braucht Rea-Sixty `SelectionMode::TotalMix` und
die sechs Stellen aus `rme-strip-and-uf8-plan.md` 4.4. Es erbt aber die bestehende Abhängigkeit
des Side-Cars von ORCs `rme.json` (Abschnitt 1). **Frage G.**

---

## 7. Hue und DynaMount

### 7.1 Herausziehen, Rea-Sixty zuerst

| Modul | aus |
|---|---|
| `src/Uf8HueFace` | Maler `main.cpp:42185-42292`, Farbbalken `:39428-39445` |
| `src/Uf8HueInput` | Fader `:27472`, Touch `:28429`, V-Pot `:28635`, Push `:28119`, SEL/CUT/SOLO `:28205-28215` |
| `src/Uf8DynaFace` | Maler `:42055-42178`, Farbbalken `:39407-39421` |
| `src/Uf8DynaInput` | Fader + Cache `:27454-27466`, Touch-Senden `:28400-28424`, V-Pot `:28624`, FLIP-Re-Park |

Rea-Sixty ruft die Module an genau den heutigen Stellen. Ob `slotLabelForVisibleSlot`
(`main.cpp:39556`) einen Aufrufer hat, beim Herausziehen klären; toten Code nicht mitnehmen.

**Strips:** ORC nimmt dieselbe Füllrichtung (links/rechts) wie Rea-Sixty, aus seinen eigenen
Einstellungen. Lampe 3 sitzt in beiden Programmen auf demselben Strip. Die übrigen Strips sind in
REAPER Spuren, in ORC dunkel.

### 7.2 Was ORC mitnimmt

| | ORC | Rea-Sixty |
|---|---|---|
| Hue: Lampen/Zonen auf Fader, V-Pot, Push, CUT, SOLO, SEL | ja | wie heute |
| Hue: Szenen (`HueScenes`, kurz = Szene, lang = dynamisch) | ja | wie heute |
| Hue: Aufnahmelicht nach REAPER-Aufnahme | nein (kein REAPER) | wie heute |
| **Hue: Aufnahmelicht nach OBS-Aufnahme** (eigene Idee) | ja | ja, als zweiter Auslöser. **Frage H** |
| Marken-Cues `hue:` / `obs:` | nein (keine Marker) | wie heute |
| DynaMount: X/Y mit FLIP, R auf V-Pot, Senden beim Loslassen, Home | ja | wie heute |
| **DynaMount: Positionsspeicher auf den Soft-Keys** (eigene Idee, lang = speichern) | ja | ja. **Frage I** |

### 7.3 Bauhinweise

- `macos_http.mm` muss in ORC **ohne ARC** kompilieren (`CMakeLists.txt:466-468` gegen ORCs
  `-fobjc-arc`, `:1145-1149`).
- Strip-Farben aus `uf8::selPaletteRgb` (`Protocol.h:225`), kommt mit `Protocol.cpp`.
- Gespeichert wird im ORC-Lauf über `takeConfigDirty()`, wie in Rea-Sixty; kein Manager fasst
  einen Speicher an.

---

## 8. Einstellungen

| | Rea-Sixty | ORC |
|---|---|---|
| Hue / OBS / DynaMount | ExtState, **unverändert** | eigene Dateien `ORC/hue.json`, `obs.json`, `dynamount.json` |
| Format | dieselben `serialize`/`deserialize` der Manager (`HueManager.cpp:696`, `ObsManager.cpp:103`, `DynaMountManager.cpp:194`) | gleich |
| Übernehmen | Knopf „Import from ORC“ in den Reitern Hue/OBS/DynaMount, sichtbar nur wenn ORCs Ordner existiert | Knopf „Import from REAPER“, sichtbar nur wenn `reaper-extstate.ini` existiert. Liest die Schlüssel, schreibt nie in REAPERs Dateien |
| Oberfläche | ImGui wie heute | AppKit-Reiter „Hue“, „OBS“, „DynaMount“: gleiche Feldnamen, gleiche Reihenfolge, Grenzen aus dem Modul |

Für jede Gruppe:

| Gruppe | geht, weil |
|---|---|
| nur Rea-Sixty | nichts ändert sich |
| nur ORC | ORC liest und schreibt nur seinen Ordner |
| beides, jede Reihenfolge | jedes läuft mit den eigenen Einstellungen; Import nur per Knopf, also kein stiller Verlust |
| beides, verschiedene Stände | jedes liest nur sein Format; der Import überspringt unbekannte Felder |
| ORC unter Windows als Store-App (später) | ORC schreibt nur in seinen Bereich; neue Dateien einer solchen App unter `AppData` sieht ein anderes Programm nicht (Microsoft-Doku), darum kein gemeinsamer Ordner |
| portables REAPER | unverändert |

Der Hue-Schlüssel kommt beim Import mit, die Bridge muss nicht neu gekoppelt werden. Passwörter
liegen in ORC im Klartext in seinem Ordner, wie heute in REAPERs INI.

Preis: wer beide hat und in einem Programm eine Lampe umstellt, muss im anderen importieren. Die
Bedienung ist trotzdem gleich.

**Verworfen** (mit Grund, damit es nicht wiederkommt):
- ORC besitzt, Rea-Sixty liest nur: Rea-Sixty wäre gesperrt, sobald ORC daliegt (Bedingung 2).
- Gemeinsamer Ordner für beide: bricht, wenn ORC vor dem neuen Rea-Sixty installiert wird
  (Übernahme übersprungen, Lampen still weg), bei Versionsschiefe (altes Programm schreibt neues
  Format kaputt), bei einer Windows-Store-App (AppData-Virtualisierung) und bei portablem REAPER
  (Bedingung 3).

Setup-Bundle (`SetupBundle.cpp`) trägt diese Werte heute nicht, obwohl der Kommentar
`main.cpp:61484` davon ausgeht. Nicht Teil dieses Plans, nur notiert.

---

## 9. OBS auf dem UF8 (neu, in beiden)

| UF8 | OBS |
|---|---|
| Soft-Keys oben | Szenen (`ObsScenes`, Lampe auf der Szene auf Sendung), mit Option aus Frage C |
| Builtins | Aufnahme, Pause, Kapitelmarke (gibt es), ORC ab Werk auf Layer 1-3 |
| **8 Fader** | **eigene Idee: OBS-Audioquellen** (`GetInputList`, `SetInputVolume`, `SetInputMute`, Rückweg `InputVolumeChanged`, `InputMuteStateChanged`) |
| CUT | Mute der Quelle |
| VU | `InputVolumeMeters` (eigenes Abo-Bit `1 << 16`) |
| Scribble | Quellenname, dB |

Namen aus der offiziellen Protokolldoku (obs-websocket `docs/generated/protocol.md`, am 29.10.
gelesen). `ObsProto` lernt vier Anfragen und drei Ereignisse, `ObsManager` hält eine Quellenliste
wie die Szenenliste. `test_obs` deckt `WsClient`/`ObsManager` heute nicht ab; der Stub
`scratchpad/obs_stub.py` vom 06.09. gehört in die Tests.

Ohne die Fader ist der OBS-Modus nur Soft-Keys, und die gibt es schon als Bank. **Frage J.**

---

## 10. Offene Fragen

| | Frage | Vorschlag |
|---|---|---|
| A | Übergabe v2 mit Quittung, eine Einstellung „Take over from ORC“ für UF1 und UF8 gemeinsam? | ja |
| B | Modus-Builtins in Rea-Sixty ab Werk auf keiner Taste (V-POT/1-5 bleiben Seiten), in ORC auf V-POT/1-5? | ja |
| C | Option „Ein Modus bringt seine Soft-Key-Bank mit“: Rea-Sixty ab Werk aus, ORC an? | ja |
| D | Kanal-Encoder am UF8 in TotalMix, wenn ein UF1 da ist: Main Volume (zweiter Weg zum selben Wert) oder Submix-Pegel? | Main Volume |
| E | Soft-Keys in TotalMix: dieselbe RME-Bank wie der UF1 (beide Hälften) oder eigene UF8-Bänke? | dieselbe Bank |
| F | Fenster folgt der UF1-Auswahl immer? | immer |
| G | TotalMix am UF8 auch in Rea-Sixty, mit der bestehenden Abhängigkeit von ORCs `rme.json` (wie der Side-Car)? Oder bekommt Rea-Sixty für TotalMix eigene Einstellungen mit Import, wie in Abschnitt 8? | eigene Einstellungen mit Import, dann gilt Bedingung 2 auch für TotalMix und der Side-Car geht auch ohne ORC |
| H | Hue-Aufnahmelicht nach OBS-Aufnahme, in beiden? | ja |
| I | DynaMount-Positionsspeicher auf den Soft-Keys, in beiden? | ja, eigene Stufe am Ende |
| J | OBS-Modus mit Audio-Fadern? | ja |
| K | ORC-Version | 2.0.0 |

Zu G: Bisher war mein Vorschlag, die Side-Car-Abhängigkeit zu lassen. Mit Abschnitt 8 ist der
saubere Weg klar und derselbe für alle Modi. Er kostet Rea-Sixty eine RME-Einstellungsseite
(die seit 25.09. entfernt ist) und widerspricht dem Entscheid vom 25.09. („die ganze Konfig für
den RME Side-Car kommt in ORC“). Darum Franks Entscheid.

---

## 11. Reihenfolge

Jede Stufe einzeln baubar, jede mit Test für die reinen Teile. Bei jeder Stufe, die Rea-Sixty
ändert, gilt: mit einem UF1, einem UF8 und ohne ORC ändert sich nichts.

| Stufe | Inhalt | Programm | Release |
|---|---|---|---|
| 1 | Übergabe v2 (`orc.pid`, `released`/`returned`, Warten, UF8-Retry, Versionszeile) | beide | **Rea-Sixty-Release, vor Stufe 4 bei Kunden** |
| 2 | `Uf8Frames` und UF8-Schlaf herausziehen, Rea-Sixty läuft darauf | Rea-Sixty | mit 1 |
| 3 | `RmeSelection` aus `input::State`, Fenster-Feld | beide | |
| 4 | UF8 in ORC: Gerät, Frames, Schlaf, Grundstellung, Menüzeile, Meldung bei alter Marke | ORC | |
| 5 | `Uf8RmeFace` + `Uf8RmeInput`, TotalMix am UF8 allein, STRIP am UF8, Reihen-Builtins | ORC | |
| 6 | UF1 Master + UF8 Extender | ORC | |
| 7 | Modus-Builtins (`selection_mode_obs`, `selection_mode_totalmix` mit Startwerten), Option aus Frage C, ORCs Tastenreiter mit Gruppenschalter | beide | |
| 8 | Hue- und DynaMount-Zweige aus `main.cpp` herausziehen, Rea-Sixty läuft darauf | Rea-Sixty | |
| 9 | Hue + DynaMount in ORC, ORCs Dateien, AppKit-Reiter, Import-Knöpfe in beiden | beide | |
| 10 | OBS-Modus nach Frage J | beide | |
| 11 | Nach Frage G: TotalMix in Rea-Sixty (`SelectionMode::TotalMix`, sechs Stellen) | Rea-Sixty | |
| 12 | Ideen nach H und I | beide | |
| 13 | Handbücher (ORC-Template braucht `data-btn` für jede UF8-Taste, CI-Prüfung „Verify the ORC manual“ erweitern; Rea-Sixty: Übergabe, Import, neue Builtins), Release ORC 2.0.0 + Rea-Sixty aus demselben Commit | beide | |

---

## 12. Nicht in diesem Plan

- Zwei UF8 in ORC (Tims Fläche), UC1 in ORC.
- Windows-ORC (`orc-mac-release-plan.md`, „Windows, später“). Abschnitt 8 hält ihm die Tür offen.
- Marken-Cues und REAPER-Aufnahmelicht in ORC, UF1-Hue in ORC.
- MCU/DAW-Steuerung und Bildschirm-Mixer in ORC (Entscheide 25.09.).
- Setup-Bundle für Hue/OBS/DynaMount.

---

## 13. Belegt und nicht belegt

**Belegt (Code bzw. Quelle in der Sitzung gelesen):** alle Zeilenangaben; ORC ohne UF8-Code;
`UF8Device`, `Protocol.cpp`, `Hue*`, `Obs*`, `WsClient`, `Sha256`, `DynaMount*`, `HttpClient` ohne
REAPER-Include; Einstellungen im ExtState; Übergabe ohne Quittung; UF8-Startfehler ohne Retry;
`input::State` gehört dem UF1-Eingang; kein `SelectionMode::TotalMix`/`Obs`; `uf1_hue` eigenes
Builtin; Side-Car hängt an `g_rmeAvailable`; obs-websocket-Namen aus der Protokolldoku;
AppData-Virtualisierung aus Microsofts Doku „Understanding how packaged desktop apps run on
Windows“.

**Nicht belegt:**
- Wie schnell ein von ORC geschlossenes UF8 unter macOS für REAPER frei ist (darum 3 s Obergrenze).
- Ob TotalMix Pegel für alle Reihen schickt, wenn „Send Peak Level“ an ist. Vor Stufe 5 an
  Franks TotalMix mitlesen (`ORC_TRACE=1`, nur lesen).
- Das obs-websocket-Abo-Bit für Input-Ereignisse; vor Stufe 10 in der Doku nachsehen.
- Ob `slotLabelForVisibleSlot` einen Aufrufer hat.
