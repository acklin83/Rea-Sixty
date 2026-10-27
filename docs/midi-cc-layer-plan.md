# MIDI-CC-Layer auf dem UF8: Plan

Stand 27.10.2026. Nichts gebaut. Franks Entscheide vom 27.10. sind eingearbeitet, das
Settings-Mockup ist abgenommen („passt so“). Was über REAPER als Tatsache dasteht, ist im
SDK-Header gelesen (`reaper_plugin_functions.h`, `reaper_plugin.h`) oder in Cockos' eigenen
Surface-Treibern (`vendor/reaper-sdk/reaper-plugins/reaper_csurf/`). Was über unseren Code
dasteht, hat eine Zeilennummer vom 27.10. Was nur vermutet ist, steht unter „Zuerst messen“.

---

## 1. Was der Layer ist

Einer der drei UF8-Layer bekommt die Art **MIDI CC**. Steht das UF8 auf diesem Layer, steuern
Fader, V-Pots, V-Pot-Druck, die acht Soft-Keys über den V-Pots, die acht SEND/PLUG-IN-Tasten
und die zwei Fussschalter keine Spuren mehr, sondern senden MIDI: Control Change, bei
SEND/PLUG-IN Program Change. Ziel sind REAPERs virtuelles MIDI-Keyboard (zum Aufnehmen in
eine Instrument-Spur) und Hardware-MIDI-Ausgänge.

Bidirektional: Ändert sich ein Wert anderswo, folgt die Fläche. Zwei Quellen:
1. CC-Kurven in den MIDI-Items der gewählten Spur, beim Abspielen.
2. Ein Hardware-MIDI-Eingang, über den ein Synth seine Reglerstellungen meldet.

Vorbild ist SSLs MIDI-CC-Layer aus 360° v1.6 (UF8 User Guide Rev 11, S. 162 bis 169). Der
sendet nur; der Rückweg ist unser Zusatz.

### Franks Entscheide (27.10.2026)

| # | Frage | Entscheid |
|---|---|---|
| 1 | Ziel | Virtuelles Keyboard UND Hardware-Ausgänge, als Häkchenliste **pro Layer** |
| 2 | Rückweg | Aus den MIDI-Items der **gewählten Spur** UND von einem Hardware-Eingang |
| 3 | Flächen | **Nur UF8** |
| 4 | Form | Ein **ganzer Layer**, Art pro Layer wählbar; übernimmt immer die ganze Fläche, egal wie „Layers switch Quicks only“ steht |
| 5 | Auflösung | 7 bit; **14 bit als Option pro Fader** (nur CC 0 bis 31, sendet CC n und n+32) |
| 6 | Presets | Ja, CC-Sätze speichern und laden |
| 7 | Namen | 8 Zeichen (`kUf8ScribbleChars`, Protocol.h:78, am 11.09. gemessen, 9 rendert nicht) |

---

## 2. Was REAPER hergibt

**Senden.** `StuffMIDIMessage(mode, b1, b2, b3)`: mode 0 = virtuelles MIDI-Keyboard, 1 =
Steuereingang (Actions, MIDI Learn), 16+N = Hardware-Ausgang N. Den Weg 16+N benutzen die
MIDI-Command-Bindings schon (`bh.sendMidi`, main.cpp:60183-60207, Begründung gegen
`CreateMIDIOutput` in main.cpp:60175-60182). Jede Spur, deren Eingang das virtuelle Keyboard
ist (oder „All MIDI inputs“), bekommt die Nachrichten von mode 0 und kann sie aufnehmen.

**Aus Items lesen.** `MIDI_CountEvts`, `MIDI_GetCC` (Position in PPQ, Kanal, CC-Nummer, Wert),
`MIDI_GetPPQPosFromProjTime`, dazu `MIDI_GetHash` zum Erkennen von Änderungen. Bisher nutzt
Rea-Sixty keine davon (gesucht 27.10.).

**Hardware-Eingang.** `CreateMIDIInput(dev)` liefert ein `midi_Input` (reaper_plugin.h):
`start()`, `stop()`, pro Tick `SwapBufs()`, dann `GetReadBuf()` und die Ereignisse mit
`EnumItems` lesen. Genau so machen es Cockos' Treiber, zum Beispiel
`csurf_bcf2000.cpp:144-192`. Geräteliste über `GetNumMIDIInputs` / `GetMIDIInputName`.
⚠ SDK-Text: „Can only reliably create midi access for devices not already opened in
prefs/MIDI“. Das ist die eine grosse Unbekannte, siehe Abschnitt 5.

---

## 3. Datenmodell

Neu in `Bindings.h`, neben `struct Layer` (Bindings.h:842-847):

```
enum class LayerKind { Reaper, MidiCc };

struct CcControl {          // ein Fader, V-Pot, Druck, Soft-Key, Fussschalter
    int  channel = 0;       // 0 = Standardkanal des Layers, sonst 1..16
    int  cc      = 0;       // 0..127 (bei SEND/PLUG-IN: Programmnummer)
    bool hiRes   = false;   // nur Fader, nur cc 0..31
    enum Behaviour { Toggle, Hold } behaviour = Toggle;   // nur Tasten
    std::string name;       // max. 8 Zeichen
    int  colour = 0;        // Palettenindex wie die Binding-Farben
};

struct CcPage {
    CcControl fader[8], vpot[8], push[8], soft[8], sendPlugin[8], foot[2];
};

struct CcLayer {
    std::vector<std::string> outputs;   // Gerätenamen; "" = virtuelles Keyboard
    std::string inputDevice;            // "" = keiner
    bool followItems = true;            // Rückweg aus den Items der gewählten Spur
    int  defaultChannel = 1;
    CcPage pages[5];
};

struct CcPreset { std::string name; CcLayer layer; };
```

`Layer` bekommt `LayerKind kind` und `CcLayer cc`. `Config` bekommt
`std::vector<CcPreset> ccPresets`.

**JSON.** Additiv, ohne Versionssprung: `"kind": "midi_cc"` und `"midi_cc": {…}` im
Layer-Objekt (`serializeLayerBody_` Bindings.cpp:2043-2077, `parseLayer_` 3035-3053), nur
geschrieben, wenn der Layer ein CC-Layer ist. Fehlt der Schlüssel, ist der Layer REAPER. Die
Presets als `"cc_presets": […]` auf oberster Ebene, nur wenn nicht leer, wie `bank_presets`
(Bindings.cpp:2244-2273, 2852-2861). So bleibt Version 55 (Bindings.cpp:3550), und ein
älteres Rea-Sixty liest die Datei weiter (es kennt die Schlüssel nicht und überspringt sie).

**Laufzeitwerte** (nicht in der JSON): pro Seite und Control der aktuelle Wert (0..127 oder
0..16383), „Wert bekannt ja/nein“, Zeitpunkt der letzten eigenen Sendung. Nur im Speicher.

---

## 4. Bausteine

### Baustein A: Sonde (vor allem anderen, siehe Abschnitt 5)

### Baustein B: Kern `MidiCcLayer.h` (reine Logik, ctest `test_midi_cc_layer`)

Ohne REAPER, ohne Gerät, wie `LaneModel.h`:
- Fader-Position (0..15583, `kUf8FaderPbMax` main.cpp:19905) in CC-Wert, 7 bit oder 14 bit
  (MSB, LSB), und zurück in eine Motorposition.
- V-Pot-Rastung in Wertänderung, geklemmt auf 0..127: eine Rastung = 1 Schritt mal
  „UF8 V-Pot speed“ (Abschnitt 6, Punkt 5), mit Fine (Shift) immer genau 1.
- Tasten: Toggle (0/127 abwechselnd), Hold (127 beim Drücken, 0 beim Loslassen).
- Nachricht bauen: Statusbyte 0xB0 / 0xC0 mit Kanal, Daten.
- Eingehende Nachrichten zuordnen: Kanal + CC auf (Seite, Control), 14-bit-Paare
  zusammensetzen (MSB setzt, LSB verfeinert).
- Wert an einer Position aus einer sortierten CC-Liste: letzter Punkt vor oder auf der
  Position.
- Echo-Regel: Während ein Control angefasst oder gerade gesendet ist (Fenster ~150 ms),
  gewinnt die Hand, eingehende Werte für dieses Control werden nur gemerkt, nicht gefahren.

### Baustein C: Datenmodell, JSON, Presets

`CcLayer` mit Serialisierung, Parser, Presets (Speichern, Laden, Umbenennen, Löschen) nach
dem Muster von `SoftKeyBankPreset` (Bindings.cpp:6938-7168). Ein atomarer Merker
`g_ccLayerActive` (auch vom USB-Thread lesbar, ohne `g_cfgMutex`), gesetzt beim
Layerwechsel (`setActiveLayer` Bindings.cpp:9834) und bei jeder Config-Änderung. Er fragt den
**rohen** Layer (`getQuickLayer`, Bindings.cpp:6326), nicht `getActiveLayer`, denn ein
CC-Layer übernimmt immer die ganze Fläche (Entscheid 4).

### Baustein D: Eingang (USB-Thread, Muster DynaMount)

Abfangen in `onUf8Input`, bevor der normale Weg greift, nach dem Vorbild von DynaMount
(Fader main.cpp:26867-26875: Wert in ein Atomic, Motorziel = Handposition, `continue`):

| Control | Stelle | Was passiert |
|---|---|---|
| Fader-Position | main.cpp:26764 ff. | Wert in `g_ccPending[strip]`, Handposition als Motorziel zurück (wie 26964) |
| Fader-Touch | main.cpp:27652 ff. | Touch-Zustand merken, **keine** Spurwahl, kein `TouchSelectExclusive` |
| V-Pot drehen | main.cpp:27882 ff. | Rastungen in eine CC-Queue |
| V-Pot-Druck | main.cpp:27415 ff. | vor Hue und Learn abfangen |
| Soft-Keys 0x18-0x1F | main.cpp:27051 ff. | vor Plugin Mode, Lanes, Quicks |
| SEND/PLUG-IN, Fussschalter | vor `dispatch()` main.cpp:27404 | Program Change bzw. CC |
| Soft-Key-Bank 1-5 | dito | Seite wählen |
| PAGE ◄ ► | dito | Seite blättern |

Alle anderen Tasten: siehe „Was REAPER bleibt“. Gesendet wird auf dem Main-Thread (REAPER-API
nur dort), im Timer, vor dem Maler.

**FLIP** tauscht auf dem CC-Layer Fader und V-Pots: der Fader fährt den V-Pot-CC, der Ring
zeigt den Fader-CC. Rein im Kern (welcher `CcControl` ist dem Fader zugeordnet).

### Baustein E: Ausgang

`StuffMIDIMessage(0, …)` für das virtuelle Keyboard, `StuffMIDIMessage(16 + N, …)` pro
gewähltem Hardware-Ausgang, Gerät per Name aufgelöst wie in `bh.sendMidi`. Den
Namen-zu-Index-Lauf aus main.cpp:60183-60207 in eine geteilte Funktion ziehen, nicht kopieren.
14 bit: erst MSB (CC n), dann LSB (CC n+32), wie die MIDI-Norm es verlangt. Nur Änderungen
senden, keine Wiederholungen desselben Werts.

### Baustein F: Anzeige (der Layer besitzt die Fläche)

Im Maler `pushZonesForVisibleSlots` (main.cpp:39832, einmal pro Tick aus 48455) ein eigener
Zweig pro Strip, nach dem Muster von DynaMount (main.cpp:40841-40963) und Hue (40971-41079):
Scribble = Name, Wertzeile = Name links, Wert rechts (`uf8ValueLine`, Uf1Text.cpp:89),
Fadermotor auf den Wert (Motorblock wie 40933-40955), Ring = V-Pot-Wert, dann `continue`.

Weitere Stellen, die heute Spurzustände malen und auf dem CC-Layer schweigen oder CC zeigen
müssen. Das ist die Liste aus der Feldkarte (Protocol.h:128-139) und dem Maler-Bericht:

| Zelle | heute | auf dem CC-Layer |
|---|---|---|
| V-Pot-Modusregister | eigene Schleife main.cpp:42416-42548, sieht das `continue` nicht | eigener Zweig: unipolar (0x01) |
| SEL-LED | Ereignis `sendLed` main.cpp:25540 (Sperre wie 25548), pro Tick nur im Plugin Mode | Farbe des Fader-CCs |
| Solo, Cut | `sendLed`, Cut pro Tick 40079-40139 | aus |
| Soft-Key-LEDs + Beschriftung | 40240-40795 | Name, leuchtet bei Wert 127 |
| SEND/PLUG-IN-LEDs | `pushUf8GlobalLeds` 44606-44625 | Farbe aus dem Control |
| Farbbalken (ColorSync) | 48420-48453 | Farbe des Fader-CCs |
| Kanalnummer, dB-Anzeige, CS-Typ | Maler | leer |
| VU-Meter | `pushVuMeter` 43534 | aus |

Beim Verlassen des Layers: `g_bankDirty`, `g_pageDirty`, `g_softKeyDirty` setzen und den
Cache der globalen LEDs leeren (main.cpp:44107/44112), wie der Encoder-Picker beim
Schliessen (39409-39419). Sonst bleiben CC-Namen stehen, bis sich zufällig etwas ändert.

**Wert noch unbekannt** (gerade auf den Layer gewechselt, nichts empfangen, nichts bewegt):
Fadermotor bleibt, wo er ist, Wertzeile zeigt `--`, es wird nichts gesendet. Erst die erste
Bewegung oder der erste Rückweg-Wert macht ihn bekannt. So springt nichts beim Umschalten.

### Baustein G: Rückweg aus REAPER (Items der gewählten Spur)

Nur wenn „Follow CC in REAPER“ an ist und REAPER abspielt oder aufnimmt.
- Spur = `GetSelectedTrack(nullptr, 0)`.
- Alle MIDI-Takes dieser Spur, die unter dem Abspielkopf liegen.
- Pro Take die CC-Ereignisse nach (Kanal, CC) sortiert zwischenspeichern, neu aufbauen nur,
  wenn sich `MIDI_GetHash` ändert. Pro Tick dann nur noch nachschlagen (Kern).
- Gefunden wird nur, was auf der aktuellen Seite einem Control zugeordnet ist; die anderen
  Seiten bekommen ihren Wert beim Blättern.
- Angefasste Controls folgen nicht (Echo-Regel im Kern). Beim Aufnehmen über eine bestehende
  Kurve gewinnt also die Hand.
- Kurvenformen zwischen zwei Punkten (linear, Bezier): zuerst Stufe auf den letzten Punkt.
  Interpolation nur, wenn Frank es will (Abschnitt 6).

### Baustein H: Rückweg von Hardware

Pro CC-Layer ein Eingangsgerät. Öffnen beim Aktivieren des Layers (`CreateMIDIInput`,
`start()`), schliessen beim Verlassen oder Gerätewechsel. Pro Tick auf dem Main-Thread
`SwapBufs`, `GetReadBuf`, `EnumItems` wie `csurf_bcf2000.cpp:188-192`. Eingehende CCs über
den Kern auf (Seite, Control) abbilden, Program Changes auf die SEND/PLUG-IN-LEDs. Meldet das
Gerät Werte, die auf einer anderen Seite liegen, werden sie für diese Seite gemerkt.

Welche Form der Eingang endgültig hat, entscheidet die Sonde (Abschnitt 5).

### Baustein I: Settings (wie im Mockup vom 27.10.)

Im Bindings-Pane, UF8-Tab (SettingsScreen.cpp:7711-7745):
- Dropdown **Layer type** (REAPER / MIDI CC) neben „Editing: Layer N“. Gilt für den Layer,
  der gerade bearbeitet wird (`s_editLayer`, 7545).
- Ist er MIDI CC, darunter der Layer-Block: **Output** (Häkchenliste: virtuelles Keyboard +
  jeder Hardware-Ausgang, ausgesteckte als *(offline)*), **Input** (ein Gerät oder keines),
  **Follow CC in REAPER**, **Default channel**, **CC preset** (Save, Save as, Load, Default).
- **Page** 1 bis 5 über dem Schaltplan.
- Klick auf ein Control im bestehenden Schaltplan (`drawUf8Vector`, 1568) öffnet den
  CC-Editor statt `drawBindingEditor` (Aufruf 8646): Kanal, CC-Nummer (bzw. Programmnummer),
  14 bit (nur Fader, nur CC 0 bis 31), Behaviour (nur Tasten), Name (8 Zeichen), Farbe.
- Was auf dem CC-Layer REAPER bleibt, ist im Schaltplan ausgegraut.
- Die Geräteauswahl der MIDI-Command-Bindings (SettingsScreen.cpp:4326-4373) in eine
  Funktion ziehen und für Output und Input benutzen, nicht nachbauen.
- Einträge im Such-Index (`kSearchIndex`, MixerWindow.cpp), Hilfetexte als Tooltip.

### Baustein J: Handbuch

Eigenes Kapitel „MIDI CC layer“: Einrichten, was REAPER bleibt, Rückweg, 14 bit, Presets,
Fallstudien aus Abschnitt 8, erst wenn gebaut und gegen den gebauten Ablauf gegangen.

### Was auf dem CC-Layer REAPER bleibt

Fest belegt wie auf Layer 1 (Auflösung über Layer 0, wie `applyLayerOption` es für „Quicks
only“ tut, Bindings.cpp:6301): Transport, Kanal-Encoder samt Druck und Encoder-Modustasten,
Zoom-Kreuz, Layer-Tasten, Modifier (Shift, Cmd, Alt, Ctrl), Automationsreihe.

Eigene Bedeutung auf dem CC-Layer: FLIP (tauscht), Soft-Key-Bank 1 bis 5 und PAGE ◄ ►
(Seiten).

Ohne Funktion auf dem CC-Layer: BANK ◄ ►, Quick 1 bis 3, Selektionsreihe, PLUGIN, 360, CHANNEL,
V-POT-Banktaste, SOLO und CUT. Grund: Sie bedienen Spuren oder Soft-Key-Bänke, und beides
zeigt der CC-Layer nicht. Welche davon doch REAPER bleiben sollen, entscheidet Frank
(Abschnitt 6).

---

## 5. Zuerst messen (Sonde, bevor irgendetwas anderes gebaut wird)

Eine versteckte Debug-Action in der dylib (`REASIXTY_DEBUG_MIDI_IN_PROBE`), die einen
Eingang 10 s lang öffnet und jedes Ereignis nach `/tmp/rea_sixty_midi_in_probe.log`
schreibt. Sie sendet nichts. Frank startet sie, ich lese das Log.

1. Gerät in REAPERs MIDI-Einstellungen **als Eingang aktiviert**: geht `CreateMIDIInput`
   trotzdem auf, und kommen Ereignisse an? macOS (Mac Studio) und Windows (StoerPC).
2. Gerät **deaktiviert**: dasselbe.
3. `StuffMIDIMessage(0, …)` erreicht eine aufnahmebereite Spur mit Eingang „Virtual MIDI
   keyboard“, und die CCs landen im Item.

### Gemessen am 27.10.2026 (macOS, Mac Studio, REAPER mit Sonde `eabf963c`)

- **3 (virtuelles Keyboard):** `StuffMIDIMessage(0, …)` erreicht eine aufnahmebereite Spur mit Eingang
  „Virtual MIDI keyboard“, die CC1-Rampe landet im Item (Frank).
- **1 (Eingang in REAPER aktiviert):** `CreateMIDIInput` OK für jedes echte Gerät, auch die vier
  UFX+-Ports. Komplete Kontrol S49: 216 Ereignisse (Noten, Pitchbend, Aftertouch, CC1). REAPERs
  Instrument-Spur auf demselben Eingang klang währenddessen weiter (Frank). Wir nehmen REAPER den
  Eingang also nicht weg.
- **2 (Eingang deaktiviert):** ebenso OK, 275 Ereignisse.
- „Virtual MIDI Keyboard“ selbst gibt `nullptr`: REAPERs eigenes Keyboard, kein Gerät. Für den Plan
  egal, es taucht in der Input-Liste des Layers nicht auf.
- **Windows 11 (StoerPC), 27.10.:** REAPER listet nur die zwölf SSL-V-MIDI-Ports und das virtuelle
  Keyboard als Eingänge (die eingebauten Loopbacks von Windows MIDI Services tauchen nicht auf, das
  Komplete Kontrol S49 MK1 startet ohne NI-Treiber nicht, Code 10). `CreateMIDIInput` OK für alle
  zwölf, auch für Port 1 bis 4, die REAPER selbst als Eingang offen hat (`midiins_h=983040` =
  Bits 16 bis 19 der oberen Hälfte = Geräte 48 bis 51; diese Lesart der reaper.ini ist abgeleitet,
  nicht dokumentiert). Daten kamen keine, es hat niemand gesendet.
  ⚠ Nicht gemessen: ein echter USB-MIDI-Treiber unter Windows. Die V-MIDI-Ports sind virtuelle
  Ports (teVirtualMIDI) und können sich anders verhalten.
- **Folge für Baustein H:** Gibt `CreateMIDIInput` `nullptr`, zeigt der Layer-Block beim Input
  „Device busy. Disable it as input in REAPER's MIDI preferences.“ und der Rückweg von Hardware
  fällt für dieses Gerät aus. Damit ist jeder Ausgang der fehlenden Messung abgedeckt.

Ausgang: Geht 1 auf beiden Systemen, bleibt Baustein H wie beschrieben. Geht 1 nicht, muss
der Nutzer das Gerät in REAPER als Eingang abschalten, oder der Rückweg läuft über ein
kleines JSFX auf einer Spur. Das entscheidet Frank nach der Messung.

---

## 6. Entscheide (Frank 27.10.2026: „nimm deine Vorschläge“)

Alle fünf wie vorgeschlagen: Tastenliste wie in Abschnitt 4 (SOLO und CUT ohne Funktion), Fader springt auf den letzten Kurvenpunkt, ohne MIDI-Items bleiben die Fader stehen, Werte werden beim Projektwechsel vergessen, V-Pot eine Rastung = 1 Schritt mal „UF8 V-Pot speed“, Fine = 1.

Die Fragen, wie sie gestellt waren:

1. **Tasten ohne Funktion:** Stimmt die Liste in „Was auf dem CC-Layer REAPER bleibt“?
   Insbesondere: Sollen SOLO und CUT als weitere CC-Tasten dienen (SSL macht das nicht)?
2. **Kurven zwischen zwei Punkten:** Fader springt auf den letzten Punkt, oder folgt er
   linear und Bezier-Kurven? Vorschlag: zuerst springen.
3. **Gewählte Spur ohne MIDI-Items:** Fader bleiben, wo sie sind. Vorschlag: ja.
4. **Werte beim Projektwechsel oder Neustart:** vergessen (wieder `--`) oder pro Projekt
   merken? Vorschlag: vergessen. Der Rückweg füllt sie wieder.
5. **V-Pot-Tempo:** eine Rastung = 1 Schritt, mit der vorhandenen „UF8 V-Pot speed“ als
   Faktor. Vorschlag: ja.

---

## 7. Reihenfolge

1. A Sonde, Messung, Entscheid zum Hardware-Eingang.
2. B Kern + ctest.
3. C Datenmodell, JSON, Presets.
4. I Settings (damit sich ein Layer überhaupt einrichten lässt).
5. D + E Eingang und Ausgang: Fader, V-Pots und Tasten senden. Ab hier am Gerät benutzbar,
   nur ohne Rückweg und mit alter Anzeige.
6. F Anzeige: Layer besitzt die Fläche, Verlassen malt sauber neu.
7. G Rückweg aus Items.
8. H Rückweg von Hardware.
9. J Handbuch mit Fallstudien.

---

## 8. Fallstudien (kommen nach dem Bau ins Handbuch, gegen den gebauten Ablauf geprüft)

### Fall 1: Streicher mit CC1 und CC11 einspielen
Layer 2 = MIDI CC, Output virtuelles Keyboard, Fader 1 = CC1 „Dynamics“ (14 bit), Fader 2 =
CC11 „Express“. Instrument-Spur, Eingang „Virtual MIDI keyboard“, aufnahmebereit. Noten mit
dem Keyboard, gleichzeitig Fader 1 und 2 fahren. Beim Abspielen fahren beide Fader die
aufgenommenen Kurven nach (Follow CC in REAPER, Spur gewählt).

### Fall 2: Eine Passage nachfahren
Wie Fall 1, Wiedergabe läuft, Fader 1 anfassen: ab dem Anfassen gewinnt die Hand, REAPER
nimmt im Überschreibmodus die neue Kurve auf, nach dem Loslassen folgt der Fader wieder der
Kurve.

### Fall 3: Hardware-Synth mit Rückmeldung
Output = Hardware-Ausgang des Synths, Input = sein Ausgang. Seite 1 = Filter, Resonanz,
Hüllkurve. Dreht man am Synth selbst, fahren die UF8-Fader mit.

### Fall 4: Zwei Klangerzeuger auf einem Layer
Kanal 1 = Kontakt, Kanal 2 = Synth, beide Ausgänge im Layer abgehakt. Seite 1 Kontakt, Seite
2 Synth, gewechselt mit den Soft-Key-Bank-Tasten.

### Fall 5: Sustain mit dem Fussschalter
Fussschalter 1 = CC64, Behaviour Hold.

---

## Quellen

- SSL UF8 User Guide Rev 11, S. 162 bis 169 (`docs/docs/SSL UF8 User Guide_Rev11.pdf`).
- SSL 360° v1.6 Ankündigung:
  https://solidstatelogic.com/media/ssl-enhances-controllers-and-plug-ins-with-latest-ssl-360°-v1.6-update
- REAPER SDK: `extension/vendor/reaper-sdk/sdk/reaper_plugin_functions.h` (StuffMIDIMessage,
  CreateMIDIInput, MIDI_GetCC, MIDI_GetPPQPosFromProjTime, GetMIDIInputName),
  `reaper_plugin.h:1395-1440` (midi_Input), `reaper-plugins/reaper_csurf/csurf_bcf2000.cpp:144-192`.
