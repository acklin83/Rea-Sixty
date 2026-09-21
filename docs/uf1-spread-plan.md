# RME-Side-Car in Rea-Sixty: der ganze Plan

Stand 21.09.2026, zweite Fassung. **Nichts davon ist gebaut.** Freigabe durch
Frank steht aus. Alles hier ist nach v0.6.0 (Grenze `87edabf`).

## ⛔ Worum es hier geht, und worum nicht

| | was | wo es läuft | dieser Plan? |
|---|---|---|---|
| 1 | **RME Monitor als Side-Car in Rea-Sixty** | in REAPER, die UF1 steuert TotalMix | **ja, nur das** |
| 2 | DAW-Ansicht auf Layout 1 | in REAPER | nein, geparkt (`docs/uf1-daw-view-layout1-plan.md`) |
| 3 | ORC, das Standalone | ohne REAPER | nein, später |

## Was Frank am 21.09. festgelegt hat

- **Alles ist einstellbar**, was wo erscheint.
- **Standard der vier V-Pots: Phones 1 bis 4.** Drehen = Lautstärke, Drücken =
  dieser Kanal kommt auf den Fader.
- **Der Kanal auf dem Fader zeigt immer seinen EQ-Graph**, Kanal-EQ, nicht
  Room EQ.
- Ports **7005 / 7006** (TotalMix OSC Remote 3). Remote 1 hat TotalReaper
  (7001/7002), Remote 2 hat stoerme (7003/7004, läuft gerade und belegt 7004).
- Soft-Keys: Dim, Mono, Speaker B, Talkback als Builtins.
- Zeitfeld: REAPER-Zeit läuft weiter.
- Fader über `faderlin`.
- Farben ordnet Frank selbst zu.

## ⛔ Daraus folgt: Layout 3, nicht Layout 1

Der EQ-Graph (`0x0122`) existiert nur in Layout 3, die vier Farbbalken
(`0x012b`) nur in Layout 1 (Runbook 21.09.). **Der EQ-Graph gewinnt**, also
läuft das Side-Car auf **Layout 3**, derselben Ebene wie heute. Das macht es
kleiner:

- kein Ebenenwechsel, kein neuer Burst
- Wertzeile (`uf1ValueLine`, 11 + 8), Stile (`0x01`/`0x08`), Kopfzellen,
  Soft-Key-Breite: alles wie heute
- die Farbe des Fader-Kanals geht auf den kleinen Balken `0x0018`, wie die
  Spurfarbe heute
- **keine vier Farbbalken** über den V-Pots

## Die Teile

### A. OSC-Client `RmeManager`

Nach dem Muster von `HueManager` (`HueManager.h:143`).

1. UDP, nicht blockierend, **ohne `select()`** und **ohne `SO_REUSEADDR`**
   (Memory `sslcore-fd-setsize-kills-reaper`, `windows-so-reuseaddr-hijacks-port`).
2. Senden an 7005, hören auf 7006 (Standard, einstellbar).
3. Beim Start `/sendall`, danach alles in `RmeState` (`ingest`).
4. Zustand: „aus", „Port belegt", „kein TotalMix", „verbunden", „keine
   Antwort mehr". **„Port belegt" ist ein eigener Zustand**, weil genau das
   passiert, wenn stoerme oder ein zweites REAPER dasselbe Paar hält.
5. Senden: `faderlin`, `volume`, Controlroom-Schalter.
   ⚠ `faderlin` ist bisher nur gelesen, nie gesendet. Vor dem Bau mit dem
   Messwerkzeug prüfen.
6. Kein REAPER-API-Aufruf auf einem Fremdthread
   (Memory `feedback-reaper-api-input-thread`).

### B. Die Belegung, einstellbar

Neue Datei **`rme.json`** neben `bindings.json` (`Bindings.cpp:2845`,
`configDir_()`), wie in der Studie 6.2 vorgesehen:

```json
{
  "connection": { "host": "127.0.0.1", "send": 7005, "receive": 7006 },
  "vpots": [
    { "target": "phones1", "turn": "volume", "push": "to_fader" },
    { "target": "phones2", "turn": "volume", "push": "to_fader" },
    { "target": "phones3", "turn": "volume", "push": "to_fader" },
    { "target": "phones4", "turn": "volume", "push": "to_fader" }
  ],
  "fader":   { "default": "main" },
  "softkeys": { "bank": 0 },
  "steps":   { "volumeDb": 0.5 }
}
```

- **`target`** ist eine Rolle (`main`, `mainB`, `phones1..4`, `talk`), die
  TotalMix selbst meldet (`/controlroom/...`, `RmeState.cpp:65` ff.), oder ein
  fester Kanal (`output:8`, `input:0`, `playback:2`). Rollen zuerst, damit ein
  anderes Interface nicht jede Zelle umschreibt.
- **`turn`**: im ersten Schritt nur `volume`. Das Feld ist da, damit später
  `gain` oder ein Submix-Knoten dazukommen, ohne das Format zu ändern.
- **`push`**: `to_fader` (Standard), `mute`, `none`.
- **Soft-Keys**: keine eigene Belegung, sondern **eine der zehn vorhandenen
  UF1-Soft-Key-Bänke** (`kUf1SoftBankCount`, `Bindings.h:762`). Damit ist die
  Belegung schon heute im Bindings-Editor änderbar, mit Plain/Shift, und
  dynamische Bänke gehen mit. `softkeys.bank` sagt nur, welche Bank das
  Side-Car zeigt. Die Werksbelegung dieser Bank: Dim, Mono, Speaker B,
  Talkback. Frage 2.
- Settings, neues Pane **„RME"**: Verbindung und Status, vier Zeilen für die
  V-Pots (Ziel, Drehen, Drücken), Fader-Standard, Soft-Key-Bank. Tooltips statt
  Hilfetext (Memory `settings-tooltips-conversion`, `settings-pane-structure`).

### C. Der Maler `uf1PaintRme_`

Neben `uf1PaintSideCar_` (`main.cpp:33328`), gleiche Form wie Item Volume.

| Zone | Inhalt |
|---|---|
| V-Pot 1-4, Wertzeile `0x010e` | Name des Ziels und dB, z. B. `Phones 1  -12.5dB` |
| V-Pot 1-4, Segmentleiste | `faderlin` 0..1, Stil `0x01` |
| V-Pot, dessen Ziel gerade auf dem Fader liegt | hervorgehoben (Frage 3) |
| EQ-Graph `0x0122` | **Kanal-EQ des Fader-Kanals**, siehe D |
| kleine Anzeige am Fader | Name, dB, Farbbalken `0x0018` des Fader-Kanals, über `uf1PaintChannelStrip_` |
| Pegel am Fader `0x0009` | `/level/out/n` und `n+1` (24,5 Hz, Studie 13.7) |
| Soft-Keys `0x0104` + LEDs | die gewählte Bank, über `uf1EmitSoftKeyRow_` |
| Kopfzeile | CELL1 `RME`, Soft-Key-Bank wie in DAW |
| Zeitfeld | REAPER-Zeit wie heute |
| Ausgang | `uf1PaintModeMenuOverlay_` |

Ist TotalMix nicht verbunden: die Wertzeilen sagen es („no TotalMix"), der
Fader bleibt stehen, der Graph flach. Ein Zustand, kein Fehler.

`uf1EmitVpotRow_` bleibt der einzige Schreiber der V-Pot-Reihe.

### D. EQ-Graph aus TotalMix

Ein zweiter **Sammler** für `Uf1EqCurve` (`Uf1EqCurve.h`), der Renderer bleibt
unberührt:

| TotalMix | `uf1eq::Band` |
|---|---|
| `eq/enable` | `Model::on` |
| `band1type` 0 / 1 / 2 / 3 | Bell / **LowShelf** / HighPass / LowPass |
| `band2` | Bell |
| `band3type` 0 / 1 / 2 / 3 | Bell / **HighShelf** / HighPass / LowPass |
| `lowcut/enable`, `lowcut/freq` | HighPass |
| `lowcut/slope` 0..3 = 6/12/18/24 dB/Okt | ⚠ der Renderer hat **keine Steilheit** |

Indizes gemessen 21.09. (Studie 13.9). Ob „Shelve" auf Band 1 Low- und auf
Band 3 High-Shelf ist, ist die naheliegende Lesart, nicht gemessen. Die
Steilheit ist Frage 4.

Ausgänge haben den Kanal-EQ (`/output/<n>/eq/...`), gelesen am 21.09. am Mac
Studio, getrennt von `roomeq`. Der Room EQ bleibt draussen.

### E. Die Bedienung

| Control | tut |
|---|---|
| V-Pot `i` drehen | `turn` des Ziels `i` (Standard Lautstärke) |
| V-Pot `i` drücken | `push` des Ziels `i` (Standard: auf den Fader) |
| Fader | `faderlin` des Fader-Kanals, Motor folgt TotalMix, Touch-Entprellung wie Item Volume |
| Soft-Keys | die gewählte Bank, also Builtins |
| Kanal-Encoder | Frage 5 |
| MODE halten | Ausgang |

⛔ **Heute gehen V-Pots und Encoder während eines Side-Cars weiter an
REAPER.** `g_uf1SideCar` kommt im Input-Drain nur beim Umschalten vor
(`main.cpp:25603`). Das RME-Side-Car fängt V-Pot-Drehen, V-Pot-Drücken und den
Encoder selbst ab. Für Item Volume ändert sich dabei nichts, sofern Frank es
nicht anders will (das wäre eine eigene Meldung).

### F. Builtins

`rme_dim`, `rme_mono`, `rme_speaker_b`, `rme_talkback` über `registerBuiltin`
(`Bindings.h:866`), jeweils mit `toggleaction`, damit Lampe und REAPER-Menü
den Zustand zeigen (Memory `reaper-actions-need-toggleaction`). Vor dem Push
`extension/tools/check_builtin_docs.py`. Sie gehen damit auch auf UF8, UC1 und
Stream Deck, ohne weiteren Code.

### G. Eintrag in die Kategorie

`Uf1SideCar::RmeMonitor = 2`, `kUf1SideCarCount = 2`, `uf1SideCarName_` gibt
`RME` für Soft-Key 2 auf der Side-Car-Seite (`main.cpp:716` ff.).

### H. Tests

`rme.json` lesen/schreiben inkl. unbekannter Felder, Zielauflösung (Rolle,
fester Kanal, versteckter Kanal), der EQ-Sammler (TotalMix-Werte zu Bändern),
Zustandsmaschine des Clients ohne Socket. Lokal, CI fährt kein ctest.

### I. Doku

Handbuchabschnitt, Tooltips, Memory, Session-Notiz, Studie verweist hierher.

## Reihenfolge

A (Client, mit Statuszeile in Settings) zuerst, weil ohne Daten nichts prüfbar
ist. Dann B (Datei und Pane), G, C und D zusammen, dann E, dann F, dann H und
I. Nach A: Messwerkzeug gegen den laufenden Client, ob REAPER dieselben Rollen
sieht.

## Was NICHT in diesem Schritt ist

- STRIP (ein Kanal, viele Parameter)
- Submix-Ansicht, `turn` auf Submix-Knoten
- Snapshots, Layouts, weitere Builtins
- vier Farbbalken über den V-Pots (Layout 1)
- TotalMix auf dem UF8, ORC, das Zusammenlegen mit stoerme

## Offene Entscheidungen für Frank

1. ~~Layout 3 statt Layout 1~~ **Entschieden 21.09.:** „Farbbalken raus, auf
   Layout mit EQ bleiben."
2. **Soft-Keys über eine der zehn vorhandenen UF1-Bänke** statt einer eigenen
   Belegung? Dann ist alles schon im Bindings-Editor änderbar. Welche Bank
   bekommt die Werksbelegung Dim / Mono / Speaker B / Talkback?
3. **Welcher Kanal liegt auf dem Fader, wenn man einsteigt**, und wie kommt man
   zurück? Vorschlag: Main. Zurück per Push auf denselben V-Pot, der ihn auf den
   Fader geholt hat.
4. **Low-Cut-Steilheit im Graph:** den Renderer um eine Steilheit erweitern
   (klein, mit Test), oder vorerst immer 12 dB/Okt zeichnen?
5. **Kanal-Encoder:** die V-Pots sind jetzt fest belegt, ein Fenster zum
   Verschieben gibt es nicht mehr. Vorschlag: der Encoder blättert den
   Fader-Kanal durch alle sichtbaren Ausgänge. Oder: nichts.

## Was belegt ist und was nicht

Belegt am 21.09.: stoerme-Ports (`stoerme/stoerme.config.json`), stoerme hält
7004 (`lsof`), Ausgänge haben Kanal-EQ (Dump), TotalMix-Indizes (Studie 13.9),
Renderer-Bandtypen (`Uf1EqCurve.h`), Datei:Zeile-Angaben per grep.

Nicht belegt: `faderlin` beim Senden, Low/High-Shelf-Lesart der Bänder, dass
TotalMix auf einem dritten Remote genauso antwortet wie auf Remote 1 (Studie
13.6 hat zwei gleichzeitige Clients gezeigt, nicht drei).
