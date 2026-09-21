# RME-Side-Car in Rea-Sixty (SPREAD auf Layout 1): der ganze Plan

Stand 21.09.2026. **Nichts davon ist gebaut.** Freigabe durch Frank steht aus.
Alles hier ist nach v0.6.0 (Grenze `87edabf`).

## ⛔ Worum es hier geht, und worum nicht

Drei Dinge benutzen dieselben Layout-1-Messungen und sind trotzdem getrennt:

| | was | wo es läuft | dieser Plan? |
|---|---|---|---|
| 1 | **RME Monitor als Side-Car in Rea-Sixty** | in REAPER, die UF1 steuert TotalMix | **ja, nur das** |
| 2 | neue REAPER-Ansichten auf Layout 1/2 (z. B. vier Spuren wie SSLs „DAW Faders") | in REAPER, die UF1 steuert REAPER | nein, eigener Plan |
| 3 | ORC, das Standalone | ohne REAPER | nein, später |

## Was SPREAD ist

Die Studie (`docs/uf1-sidecar-rme-feasibility.md`, Abschnitt 8.1): **viele
Kanäle, ein Parameter.** Vier V-Pots zeigen vier Nachbarkanäle von TotalMix,
ein Push wählt einen davon, der Fader fährt den gewählten. Mit dem
Geltungsbereich `controlroom` ist das der Monitor-Controller, also der Job der
RME ARC USB.

Layout 1 ist dafür die Ebene, weil sie als einzige **vier Farbbalken über den
V-Pots** hat (`0x012b`) und daneben die ganze V-Pot-Reihe zeigt. Vermessen am
21.09., `docs/uf1-layout-probe-runbook.md`.

## Was heute schon da ist, und was nicht

| Teil | Stand | Beleg |
|---|---|---|
| OSC-Codec, Zustand, Rollen, Pegel | gebaut, getestet | `RmeOsc.{h,cpp}`, `RmeState.{h,cpp}`, `tests/test_rme_osc.cpp` |
| in die dylib gelinkt | ja | `CMakeLists.txt:359` |
| **im Tick benutzt** | **nein, kein einziger Aufruf in `main.cpp`** | grep 21.09. |
| Side-Car-Kategorie, Einstieg Shift + MODE + Soft-Key | gebaut | `Uf1SideCar` (`main.cpp:716`) |
| Schirmübergabe, Ausgang, kleine Anzeige | gebaut | `uf1HandOverScreen_`, `uf1PaintModeMenuOverlay_`, `uf1PaintChannelStrip_` |
| TotalMix-Farben und Typ-Indizes | gemessen | Studie 13.9 |
| Layout 1: Felder, Breiten, Stile | gemessen | Runbook |

⇨ **SPREAD ist deshalb zwei Dinge, nicht eines:** zuerst ein OSC-Client, der
im Tick läuft, dann der Maler. Ohne den Client hat der Maler nichts zu zeigen.

## Die Teile

### A. Der OSC-Client (`RmeManager`)

Nach dem Muster von `HueManager` (`HueManager.h:143`).

1. Eigener UDP-Socket, nicht blockierend, **ohne `select()`**
   (Memory `sslcore-fd-setsize-kills-reaper`) und **ohne `SO_REUSEADDR`**
   (Memory `windows-so-reuseaddr-hijacks-port`). Das Messwerkzeug setzt es, der
   Client darf es nicht übernehmen.
2. Beim Start `/sendall`, danach alles, was TotalMix von sich aus meldet, in
   `RmeState` (`ingest`).
3. Verbindungszustand: „kein TotalMix", „verbunden", „keine Antwort mehr".
   Wiederholung von `/sendall` nach einem Abbruch.
4. Senden: `/output/<n>/volume` (oder `faderlin`, siehe offene Frage 5).
5. Kein REAPER-API-Aufruf auf einem Fremdthread
   (Memory `feedback-reaper-api-input-thread`).
6. Settings, Pane „RME": Ein/Aus, Host, Sendeport, Empfangsport,
   Statuszeile. Tooltips statt Hilfetext (Memory `settings-tooltips-conversion`).
7. Test: Fensterlogik und Zustand ohne Socket.

### B. Die Ebene

1. Beim Eintritt zweistufig auf `{01,00}` schalten (zweimal `{00,01}`, dann
   das Ziel), wie die Sonde.
2. Beim Austritt `g_uf1PlaneLost` wie jeder Besitzer
   (Memory `uf1-screen-owning-mode-checklist`). Die Sonde ist heute den ganzen Tag
   so aus Layout 1 zurück, das hat getragen.
3. `0x011a` auf `02` lassen, sonst verschwinden CELL1 und CELL2.
4. **Alles, was Layout 1 hinterlässt, beim Austritt leeren**, weil die
   Firmware Werte hält: `0x010b` (siehe offene Frage 2), `0x012b`.

### C. Der Maler `uf1PaintSpread_`

Pro V-Pot `i` im Fenster:

| Feld | Inhalt | Grenze (gemessen) |
|---|---|---|
| `0x012b` Farbbalken | TotalMix-Farbe des Kanals | Zuordnung offen, Frage 1 |
| `0x010b` Text | Kanalname | **8 Zeichen fest** |
| `0x010e` Wertzeile | Pegel, z. B. `-12.5 dB` | 14 Gross- / 18 Kleinbuchstaben, Pixelbreite |
| `0x010f` Segmentleiste | Faderstellung 0 bis 100 | |
| `0x010d` Stil | **`0x02` Füllung von links** | `0x01`/`0x08` verstecken die Reihe |

⛔ **`uf1VpotBar_` setzt `0x01`/`0x08` und darf dafür nicht benutzt werden**,
und `uf1ValueLine` (11 + 8) passt in Layout 1 nicht. Beide bekommen eine
Layout-1-Fassung, der Layout-3-Weg bleibt unberührt. `uf1EmitVpotRow_` bleibt
der einzige Schreiber der Reihe.

Rest der Fläche:

| Zone | Inhalt |
|---|---|
| CELL1 (10 Zeichen) | Geltungsbereich, z. B. `CONTROL RM` |
| CELL2 (5 Zeichen) | Fensterlage, z. B. `1-4/6` |
| Soft-Keys (12 Gross- / 15 Kleinbuchstaben) | Frage 3 |
| Zeitfeld | Frage 4 |
| kleine Anzeige am Fader | Name und dB des gewählten Kanals, über `uf1PaintChannelStrip_` |
| Pegel am Fader `0x0009` | `/level/out/n` und `n+1` des gewählten Kanals (24,5 Hz, gemessen) |

### D. Die Bedienung

| Control | tut |
|---|---|
| V-Pot `i` drehen | Pegel von Kanal `i` im Fenster |
| V-Pot `i` drücken | Kanal `i` wird der gewählte |
| Fader | Pegel des gewählten Kanals, Motor folgt TotalMix |
| Kanal-Encoder | Fenster um einen Kanal verschieben |
| MODE halten | Ausgang, wie bei jedem Side-Car |

⛔ **Heute gehen V-Pots und Encoder während eines Side-Cars weiter an REAPER.**
Item Volume nimmt nur Schirm und Fader. SPREAD muss die Eingaben selbst
abfangen, sonst dreht ein V-Pot eine REAPER-Spur, während der Schirm einen
TotalMix-Kanal zeigt. Das ist eine eigene Stelle im Input-Drain.

### E. Eintrag in die Kategorie

`Uf1SideCar::RmeMonitor = 2`, `kUf1SideCarCount = 2`, Name in
`uf1SideCarName_` (`RME`, Soft-Key 2 auf der Side-Car-Seite).

### F. Doku

Handbuchabschnitt, Settings-Tooltips, Memory, Session-Notiz.

## Geltungsbereich für diesen Schritt

**Nur `controlroom`:** Main, Main B, Phones 1 bis 4, die Rollen, die TotalMix
selbst meldet (`/controlroom/mainout` usw., Studie 13.2). Sechs Kanäle, also
zwei Fensterlagen. `outputs`, `inputs` und `submix` kommen später auf denselben
Maler.

## Was NICHT in diesem Schritt ist

- STRIP (ein Kanal, viele Parameter, EQ-Graph)
- Submix-Ansicht
- der Builtin-Katalog (`rme_dim`, `rme_mono`, ...) und Soft-Key-Bänke
- Snapshots, Layouts
- `rme.json`
- TotalMix auf dem UF8
- das Standalone ORC

## Offene Fragen, die Frank entscheidet

1. ~~Farbbalken~~ **Entschieden 21.09.: Frank ordnet die 8 TotalMix-Farben
   selbst den UF1-Palettenindizes zu** („die farben kann ich noch selbst").
   Dafür die Sonde, Element 4, Palettenstart durchschalten.
2. **Rendert `0x010b` in Layout 3?** Wenn ja, bleibt nach dem Austritt Text
   über den V-Pots stehen. Eine Sondenrunde klärt das (Element 12 auf L3 S0).
3. **Soft-Keys:** leer lassen, oder schon Dim / Mono / Speaker B / Talkback?
   Letzteres zieht vier Builtins in diesen Schritt.
4. **Zeitfeld:** REAPER-Zeit weiterlaufen lassen, leer, oder etwas von RME?
5. **Fader-Skala:** `volume` in dB mit unserer Fader-Kurve, oder `faderlin`
   0..1 mit TotalMix' eigener Kurve (Studie: dieselbe Skala wie der
   UF1-Fader)? `faderlin` heisst: der UF1-Fader steht genau dort, wo der
   TotalMix-Fader steht.
6. **Ports:** TotalReaper belegt Remote 1 (7001/7002, `TotalReaper/src/main.cpp:32`).
   Welches Remote bekommt Rea-Sixty? Das muss in TotalMix unter Settings, OSC
   eingetragen werden.
7. **Kanal-Encoder:** Fenster um einen Kanal verschieben, oder um vier?

## Reihenfolge

A, dann E, dann B und C zusammen, dann D, dann F. A zuerst, weil jeder spätere
Schritt ohne Daten nicht prüfbar ist. Nach A kann das Messwerkzeug gegen den
laufenden Client prüfen, ob REAPER dieselben Rollen sieht.

## Was hier aus dem Code belegt ist und was nicht

Belegt am 21.09. per grep oder Messung: alles mit Datei:Zeile oder Verweis aufs
Runbook, die Ports von TotalReaper, dass `main.cpp` kein `rme::` aufruft, dass
Side-Car heute keine V-Pot-Eingaben abfängt (`g_uf1SideCar` kommt im
Input-Drain nur beim Umschalten vor, `main.cpp:25603`).

Nicht geprüft: ob der direkte Wechsel von Layout 1 auf `{03,00}` in jedem Fall
sauber ist (nur heute mit der Sonde erlebt), ob `faderlin` beim Senden
angenommen wird (nur gelesen, nie geschrieben).
