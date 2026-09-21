# RME-Side-Car in Rea-Sixty: der ganze Plan

Stand 21.09.2026, dritte Fassung. **Nichts davon ist gebaut.** Freigabe durch
Frank steht aus. Alles hier ist nach v0.6.0 (Grenze `87edabf`).

## ⛔ Worum es hier geht, und worum nicht

| | was | wo es läuft | dieser Plan? |
|---|---|---|---|
| 1 | **RME Monitor als Side-Car in Rea-Sixty** | in REAPER, die UF1 steuert TotalMix | **ja, nur das** |
| 2 | DAW-Ansicht auf Layout 1 | in REAPER | nein, geparkt (`docs/uf1-daw-view-layout1-plan.md`) |
| 3 | ORC, das Standalone | ohne REAPER | nein, später |

## Franks Festlegungen (21.09.)

1. **Alles ist einstellbar**, was wo erscheint.
2. **Layout 3 mit EQ-Graph, keine Farbbalken.**
3. **Ports 7005 / 7006**, TotalMix OSC Remote 3. (Remote 1 TotalReaper
   7001/7002, Remote 2 stoerme 7003/7004.) Remote 3 und 4 antworten wie 1 und 2.
4. **Eigene Soft-Key-Bänke** für das Side-Car, nicht die vorhandenen
   UF1-Bänke. Werksbelegung: Dim, Mono, Speaker B, Talkback.
5. **Drei Reihen wie in TotalMix:** Input, Software-Playback, Output.
   **MODE + Kanal-Encoder** zeigt die Liste der drei Reihen, genau wie heute die
   Encoder- und Jog-Modi. **Kanal-Encoder allein** blättert durch die Kanäle der
   gewählten Reihe.
6. **Der Fader liegt immer auf dem gewählten Kanal.**
7. **Das Jog-Rad ist die Main-Out-Lautstärke.**
8. **Der EQ-Graph zeigt immer den Kanal-EQ des Kanals auf dem Fader**, nie den
   Room EQ.
9. **V-Pots, Standard Phones 1 bis 4:** Drehen = Lautstärke, Drücken = dieser
   Kanal wird der gewählte (also Fader und EQ-Graph).
10. **Low-Cut-Steilheit herausfinden und richtig zeichnen.**
11. Band 1 „Shelve" = Low-Shelf, Band 3 „Shelve" = High-Shelf.
12. Zeitfeld: REAPER-Zeit läuft weiter. Fader über `faderlin`.

## Fakten, die der Plan braucht (am 21.09. nachgesehen)

| Fakt | Beleg |
|---|---|
| Ausgänge haben einen Kanal-EQ (`/output/<n>/eq/...`, `lowcut/...`), getrennt von `roomeq` | Dump Mac Studio |
| Eingänge haben einen Kanal-EQ (`/input/<n>/eq/...`) | Dump |
| **Software-Playbacks haben keinen EQ** (nur stereo, name, mute, phase, msproc, width, color) | Dump |
| Lautstärke eines Ausgangs: `/output/<n>/volume` (dB), `faderlin` | Studie 13 |
| **Lautstärke eines Eingangs oder Playbacks ist sein Knoten im Submix:** `/mix/in/<n>/<submix>/fader`, `/mix/pb/<n>/<submix>/fader` (dB). `faderlin` nimmt TotalMix laut Spezifikation an, gemeldet wird nur `fader` | Dump + Studie 7 |
| Farbe 0 = in diesem Remote versteckt | Studie 13.9 |
| stoerme läuft und hält 7004 | `lsof` |
| Renderer zeichnet den Hochpass fest mit ~12 dB/Okt (2. Ordnung) | `Uf1EqCurve.cpp:37` |
| Encoder- und Jog-Liste in der Kopfzeile | `uf1SetEncoderList_` (`main.cpp:23598`), `uf1SetJogList_` (`main.cpp:23614`) |

## ⛔ Was „der gewählte Kanal" in jeder Reihe heisst

| Reihe | Fader fährt | EQ-Graph |
|---|---|---|
| **Output** | `/output/<n>/faderlin` | Kanal-EQ des Ausgangs |
| **Input** | Knoten `/mix/in/<n>/<submix>` | Kanal-EQ des Eingangs |
| **Playback** | Knoten `/mix/pb/<n>/<submix>` | **flach, Zeile „no EQ"** |

`<submix>` ist der **zuletzt gewählte Ausgang**, genau wie in TotalMix: wer
einen Ausgang anklickt, wählt damit den Submix, in den die Eingangs- und
Playback-Fader schreiben. Vor dem ersten Ausgang: Main.

## Die Teile

### A. OSC-Client `RmeManager`

Nach dem Muster von `HueManager` (`HueManager.h:143`).

1. UDP, nicht blockierend, **ohne `select()`**, **ohne `SO_REUSEADDR`**
   (Memory `sslcore-fd-setsize-kills-reaper`, `windows-so-reuseaddr-hijacks-port`).
2. Senden an 7005, hören auf 7006, beides einstellbar.
3. `/sendall` beim Start, dann alles in `RmeState`. **`RmeState` lernt die
   Submix-Knoten** (`/mix/in|pb/<n>/<bus>/fader`) und den **Kanal-EQ** pro Kanal
   (heute werden beide nicht abgelegt, das Messwerkzeug zählte sie unter
   „no home for").
4. Zustände: aus, Port belegt, kein TotalMix, verbunden, keine Antwort mehr.
   „Port belegt" ist eigen, weil es genau dann passiert, wenn stoerme oder ein
   zweites REAPER dasselbe Paar hält.
5. Senden: `faderlin` (Ausgang und Knoten), Controlroom-Schalter.
   ⚠ `faderlin` ist bisher nur gelesen, nie gesendet: vor dem Bau mit dem
   Messwerkzeug einmal senden und die Rückmeldung lesen.
6. Kein REAPER-API-Aufruf auf einem Fremdthread.

### B. Einstellungen

**`rme.json`** neben `bindings.json` (`configDir_()`, `Bindings.cpp:2845`):

```json
{
  "connection": { "host": "127.0.0.1", "send": 7005, "receive": 7006 },
  "vpots": [
    { "target": "phones1", "turn": "volume", "push": "select" },
    { "target": "phones2", "turn": "volume", "push": "select" },
    { "target": "phones3", "turn": "volume", "push": "select" },
    { "target": "phones4", "turn": "volume", "push": "select" }
  ],
  "jog":   { "target": "main", "stepDb": 0.5 },
  "start": { "row": "output", "channel": "main" },
  "steps": { "vpotDb": 0.5 }
}
```

- `target`: eine Rolle (`main`, `mainB`, `phones1..4`, `talk`), die TotalMix
  selbst meldet (`RmeState.cpp:65` ff.), oder ein fester Kanal
  (`output:8`, `input:0`, `playback:2`).
- `turn`: `volume`. `push`: `select` (Standard), `mute`, `none`.
- Settings, neues Pane **„RME"**: Verbindung und Status, die vier V-Pots
  (Ziel, Drehen, Drücken), Jog-Ziel, Start. Tooltips statt Hilfetext
  (Memory `settings-tooltips-conversion`, `settings-pane-structure`).

### C. Eigene Soft-Key-Bänke

- Neuer Speicher in **`bindings.json`**: Side-Car-RME-Bänke, je 4 Tasten,
  Plain und Shift, wie die UF1-Bänke gebaut, aber eigene Liste. Damit gehen
  jede Aktion, jedes Builtin, Long-Press und Doppelklick, wie überall.
- **Editor:** eine eigene Matrix im Bindings-Editor unter UF1, „Side-Car RME".
- **◄ ►** blättert im Side-Car durch die eigenen Bänke, die Kopfzeile zeigt
  `N/M`.
- **Werksbank 1:** `rme_dim`, `rme_mono`, `rme_speaker_b`, `rme_talkback`.
- Bestehende `bindings.json` bekommen die Werksbank beim Laden über das
  vorhandene `version`-Feld (`Bindings.cpp:2890`), ohne Franks eigene
  Belegungen anzufassen (Memory `franks-bindings-are-not-factory`).
- Offen: wie viele Bänke (Frage 1).

### D. Die Reihenliste auf MODE + Encoder

- Im Side-Car zeigt MODE halten + Kanal-Encoder die Liste
  **INPUT · PLAYBACK · OUTPUT** statt der Encoder-Modi, in derselben Form wie
  `uf1SetEncoderList_`. MODE halten + Soft-Key bleibt der Ausgang aus dem
  Side-Car.
- Die Reihe merkt sich pro Reihe ihren letzten Kanal, so dass ein
  Reihenwechsel dorthin zurückkehrt, wo man war.

### E. Der Maler `uf1PaintRme_`

Neben `uf1PaintSideCar_` (`main.cpp:33328`), gleiche Form wie Item Volume.

| Zone | Inhalt |
|---|---|
| V-Pot 1-4, Wertzeile | Name des Ziels und dB, z. B. `Phones 1  -12.5dB` |
| V-Pot 1-4, Segmentleiste | Lautstärke, Stil `0x01` |
| EQ-Graph | Kanal-EQ des gewählten Kanals (F) |
| kleine Anzeige am Fader | Reihe, Name, dB, Farbbalken `0x0018` des gewählten Kanals, über `uf1PaintChannelStrip_` |
| Pegel am Fader `0x0009` | `/level/in|pb|out/<n>` und `n+1` des gewählten Kanals |
| Soft-Keys + LEDs | die eigene Bank, über `uf1EmitSoftKeyRow_` |
| Kopfzeile | CELL1 Reihe (`INPUT` / `PLAYBACK` / `OUTPUT`) bzw. die Liste bei MODE + Encoder; Bank `N/M` |
| Zeitfeld | REAPER-Zeit |
| Ausgang | `uf1PaintModeMenuOverlay_` |

- Kein TotalMix: die Wertzeilen sagen „no TotalMix", Fader steht, Graph flach.
- Ein V-Pot, dessen Ziel versteckt oder nicht zugewiesen ist (heute bei Franks
  Phones 3 und 4 auf Remote 1 der Fall): leer, mit „hidden" bzw. „--".
- `uf1EmitVpotRow_` bleibt der einzige Schreiber der V-Pot-Reihe.

### F. EQ-Graph aus TotalMix

Zweiter Sammler für `Uf1EqCurve`:

| TotalMix | `uf1eq::Band` |
|---|---|
| `eq/enable` | `Model::on` |
| `band1type` 0/1/2/3 | Bell / LowShelf / HighPass / LowPass |
| `band2` | Bell |
| `band3type` 0/1/2/3 | Bell / HighShelf / HighPass / LowPass |
| `lowcut/enable` + `freq` + `slope` | HighPass mit **Ordnung 1..4** |

- **Renderer:** `Band` bekommt eine Filterordnung, `hpfDb`/`lpfDb` rechnen
  mit ihr. Standard bleibt 2, damit der REAPER/SSL-Graph unverändert bleibt.
  Test in `tests/test_uf1_eq.cpp`: 6/12/18/24 dB pro Oktave weit unter der
  Eckfrequenz, und der bestehende Fall unverändert.
- **Steilheit herausfinden (Messung M1):** belegt ist nur Index 1 = 12 dB/Okt.
  Frank stellt 6, 18 und 24 einzeln ein, das Messwerkzeug liest den Index mit.
- Ob TotalMix' Low Cut ein Butterworth-Hochpass n-ter Ordnung ist oder eine
  andere Kurvenform hat, weiss ich nicht. Gezeichnet wird n × 6 dB/Okt.
- **Graph neu senden, sobald sich ein EQ-Wert des gewählten Kanals ändert**,
  auch wenn er in TotalMix am Bildschirm gedreht wird.

### G. Bedienung

| Control | tut |
|---|---|
| Kanal-Encoder | nächster/voriger sichtbarer Kanal der Reihe (Farbe ≠ 0) |
| MODE + Kanal-Encoder | Reihe wählen (D) |
| V-Pot `i` drehen | Lautstärke des Ziels `i` |
| V-Pot `i` drücken | Ziel `i` wird der gewählte Kanal (Reihe springt mit) |
| Fader | Lautstärke des gewählten Kanals (Tabelle oben), Motor folgt TotalMix, Touch-Entprellung wie Item Volume |
| Jog | Main-Out-Lautstärke, Schritt aus `rme.json` |
| Soft-Keys, ◄ ► | eigene Bänke (C) |
| MODE halten + Soft-Key | Ausgang |

⛔ **Heute gehen V-Pots, Encoder und Jog während eines Side-Cars an REAPER.**
`g_uf1SideCar` kommt im Input-Drain nur beim Umschalten vor
(`main.cpp:25603`). Das RME-Side-Car fängt alle fünf selbst ab: V-Pot drehen,
V-Pot drücken, Encoder, MODE + Encoder, Jog. Scrub + Jog (die Jog-Modus-Liste)
ist im Side-Car aus, weil das Jog hier nur eines tut. Item Volume bleibt, wie es
ist.

### H. Builtins

`rme_dim`, `rme_mono`, `rme_speaker_b`, `rme_talkback` über `registerBuiltin`
(`Bindings.h:866`), mit `toggleaction` (Memory
`reaper-actions-need-toggleaction`). Vor dem Push
`extension/tools/check_builtin_docs.py`. Damit auch auf UF8, UC1, Stream Deck.

### I. Kategorie

`Uf1SideCar::RmeMonitor = 2`, `kUf1SideCarCount = 2`, `uf1SideCarName_` = `RME`
(Soft-Key 2 der Side-Car-Seite, `main.cpp:716` ff.).

### J. Tests

`rme.json` lesen/schreiben (unbekannte Felder bleiben), Zielauflösung (Rolle,
fester Kanal, versteckt, nicht zugewiesen), Kanal-Blättern (versteckte
überspringen, Reihenende), Submix-Knoten für Input/Playback, EQ-Sammler,
Filterordnung im Renderer, Zustandsmaschine des Clients ohne Socket. Lokal, CI
fährt kein ctest.

### K. Doku

Handbuchabschnitt, Tooltips, Memory, Session-Notiz.

## Messungen vor dem Bau

| | was | wer |
|---|---|---|
| M1 | Low-Cut-Steilheit: Index für 6, 18, 24 dB/Okt | Frank klickt in TotalMix, Messwerkzeug liest |
| M2 | `faderlin` senden (Ausgang und Submix-Knoten) und die Rückmeldung lesen | Messwerkzeug, braucht einen Sende-Modus |
| M3 | Remote 3 in TotalMix anlegen (7005/7006), `/sendall` darauf | Frank legt an, Messwerkzeug fragt |

## Reihenfolge

M1 bis M3. Dann A (Client mit Statuszeile), dann I, dann E + F + G zusammen
(ohne Soft-Keys), dann C + H (Bänke und Builtins), dann D, dann J und K.

## Was NICHT in diesem Schritt ist

- STRIP (ein Kanal, viele Parameter), Room EQ
- Snapshots, Layouts, weitere Builtins
- Pan, Solo, Mute auf dem Fader-Kanal (ausser `push: mute`)
- Farbbalken über den V-Pots
- TotalMix auf dem UF8, ORC, Zusammenlegen mit stoerme

## Entschieden 21.09. („ja mach")

1. **10 Side-Car-Bänke**, wie die UF1-Bänke.
2. **Jog: 0,5 dB pro Rastung** auf Main.
3. **Playback ohne EQ: Graph flach, „no EQ".**

## (Frühere) offene Entscheidungen

1. **Wie viele Side-Car-Bänke?** Vorschlag: 10 wie die UF1-Bänke, damit der
   Editor dieselbe Form hat.
2. **Main-Volume auf dem Jog:** Schrittweite 0.5 dB pro Rastung als Standard?
3. **Playback ohne EQ:** Graph flach mit „no EQ" in der Kopfzeile, oder den
   Graph des Submix-Ausgangs zeigen? Vorschlag: flach, weil ein fremder Graph
   lügt.

## Was belegt ist und was nicht

Belegt am 21.09.: alles in der Faktentabelle, alle Datei:Zeile-Angaben per
grep.

Nicht belegt: `faderlin` beim Senden (M2), die Steilheiten 6/18/24 (M1), die
genaue Kurvenform des Low Cut, dass `/mix/.../faderlin` angenommen wird (nur
Spezifikation).
