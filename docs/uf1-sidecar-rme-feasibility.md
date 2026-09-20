# Machbarkeitsstudie: UF1 Side-Car und RME Monitor Control

> ⛔ **Alles hier ist NACH v0.6.0.** Frank, 19.09.2026: "alles bis und mit
> gestern soll in den v0.6 tag, das sidecar zeugs ist für nachher". Die Grenze
> ist `87edabf` (18.09. 17:04); der Tag geht auf diesen Commit, nicht auf die
> Spitze von main. Alles ab `539bce2` (19.09. 15:47), also diese Studie, die
> Layout-Sonde, das V-Pot/EQ-Aufräumen und das RME-Fundament, steht zwar auf
> main, gehört aber nicht in 0.6.0.

Stand 2026-09-19. Alles hier ist gelesen, nicht erinnert. Grundlage fuer den
OSC-Teil ist die offizielle Tabelle **"OSC-Commands TotalMix FX 2.1 beta 2
Global OSC", `OSCProtocoll_260721.ods`, 21.07.2026**, die Frank beigesteuert
hat (liegt bei ihm unter `~/Downloads/`, nicht im Repo, weil sie RME gehoert).
Dazu die aeltere Legacy-Tabelle von RMEs Downloadseite, die Pfade aus Franks
eigenem TotalReaper-Repo und die Rea-Sixty-Stellen mit Zeilennummern.

---

## 1. Die ARC USB ist kein OSC-Gerät

`rme-audio.de/de_arc-usb.html` sagt: USB 1.1, **MIDI Remote Control**, SysEx ab
Firmware 7. Im Computer-Modus sind Knopf und Taster ueber den Key-Commands-Dialog
von TotalMix FX frei belegbar. Im Standalone-Modus liegt eine feste Belegung
drauf (Reihe 1+2 Setups 1-6, Reihe 3 Mono / Phones 1 / Phones 2 / DIM, unten
Record / Play-Pause / Stop).

Das heisst: die ARC redet nicht OSC, sie ist ein MIDI-Gerät, das TotalMix
intern auf seine eigenen Key Commands legt. Unser Weg ist ein anderer, und
er fuehrt an derselben Stelle raus: OSC deckt denselben Funktionsvorrat ab,
nur ueber eine offene Schnittstelle statt ueber eine geraetegebundene.

## 2. TotalMix hat ZWEI OSC-Protokolle

Das ist der wichtigste Punkt der ganzen Studie, weil im Netz beide
durcheinandergehen.

**Legacy (paged), Stand 1.96:** die Tabelle aus
`rme-audio.de/downloads/osc_table_totalmix_new.zip`. Mackie-artig, bankbasiert,
Adressen wie `/1/volume3`. Was drauf liegt, haengt davon ab, welcher Bus und
welcher Bankstart gerade gewaehlt ist. Bis zu vier unabhaengige Remotes,
Ports typisch 7001/9001 und 7002/9002.

**Global OSC, ab TotalMix FX 2.1:** direkte, absolute Adressierung ohne Bank.
Das ist das Protokoll, das TotalReaper spricht. Standardports 7004/9004, ein
zweiter Controller laut RME-Forum auf 7008/9008. Mehrere Clients gleichzeitig
sind ausdruecklich vorgesehen, Zustandsaenderungen werden an alle
zurueckgemeldet.

### Die Spezifikation liegt vor

Frank hat am 19.09. die offizielle Tabelle geliefert:
`OSCProtocoll_260721.ods`, "OSC-Commands TotalMix FX 2.1 beta 2 Global OSC",
Stand 21.07.2026. Damit ist nichts an diesem Abschnitt mehr geraten. Die
Legende kennt Rot fuer "not implemented yet"; in dieser Fassung traegt genau
eine Zelle die Formatierung, und die ist leer.

Grundregeln aus dem Blatt: Kanalnummern zaehlen ab 0, Presets und Snapshots ab 1,
Stereokanaele werden ueber die linke Nummer adressiert (Ausnahmen in der Spalte
L/R), Werte kommen auch als T/F und als Integer an, die Kanal-Layout-
Einstellungen von TotalMix gelten fuer Global OSC mit, und **"Follow Submix"
sollte aus sein**.

### Was Frank will, und wo es liegt

| Wunsch | Pfad | Typ | Anmerkung |
|---|---|---|---|
| Main Out Lautstaerke | `/output/<n>/volume` | f, dB | oder `/output/<n>/faderlin`, 0..1, mit der Kurve aus dem Blatt "Fader curve" |
| Phones-Lautstaerken | `/output/<n>/volume` | f | welcher Ausgang das ist, sagt `/controlroom/phones1` bis `phones4` |
| Welcher Bus ist Main / Main B | `/controlroom/mainout`, `/mainoutb` | f | Kanalnummer, nicht Pegel |
| Dim, und um wieviel | `/controlroom/dim`, `/dimreduction` | f | |
| Mono | `/controlroom/mainmono` | f | |
| Speaker B, A/B verkoppelt | `/controlroom/speakerb`, `/linkab` | f | |
| Talkback, Kanal | `/controlroom/talkback`, `/talkchannel` | f | |
| Recall, Recall-Pegel | `/controlroom/recall`, `/recallvolume` | f | |
| Ext In, Kanal, Gain | `/controlroom/externalin`, `/extinchannel`, `/extingain` | f | |
| FX stummschalten | `/controlroom/mutefx` | f | |
| Cue | `/controlroom/cuechan` | f | |
| Snapshots | `/snapshot/load/<n>`, `/snapshot/save/<n>` | (f) | Empfang nur Wert 1; **TotalMix sendet zurueck: 0 aus, 2 aktiv, 3 geaendert** |
| Layouts | `/layout/load/<n>` | (f) | nur Empfang, seit Alpha 7 |
| Mute-, Solo-, Fadergruppen | `/mutegroup/<n>`, `/sologroup`, `/fadergroup` | f | |
| Global Mute / Solo | `/globalmute`, `/globalsolo` | f | |
| Undo / Redo | `/undo`, `/redo` | (f) | |
| TotalMix-Fenster zeigen | `/showwindow` | f | 0 aus, 1 an |
| **Pegel** | `/level/in/<n>`, `/level/pb/<n>`, `/level/out/<n>` | f, dB | Peak, nur Sendung, nur bei Aenderung |
| Geraet, Verbindung, DSP | `/status/device`, `/status/connection`, `/status/dsp` | s, f, f | |
| DuRec | `/durec/play|pause|stop|record|next|previous`, `/time`, `/state` | (f), s | Zeit und Zustand als Text |
| Zustand anfordern | `/sendall`, `/sendstate`, `/sendsettings`, `/sendchan/output/<n>` | (f) | beim Start einmal, danach nach Bedarf |

Zwei Dinge daran sind mehr, als die ARC kann:

- `/snapshot/load/<n>` **meldet zurueck**, ob ein Snapshot aktiv oder geaendert
  ist. Damit bekommen die Soft-Keys eine ehrliche Lampe, inklusive
  "geaendert, nicht gespeichert".
- `/output/<n>/faderlin` ist **0 bis 1 mit dokumentierter Kurve**. Das ist
  genau die Skala des UF1-Faders (15 Bit, 0 bis 0x7FFF). Motorposition und
  OSC-Wert sind dieselbe Zahl in anderer Aufloesung, ohne dB-Umweg.

Im Legacy-Protokoll heissen dieselben Dinge `/1/mainDim`, `/1/mainMono`,
`/1/mainSpeakerB`, `/1/mainTalkback`, `/1/mastervolume`, `/3/snapshots/8/1`
bis `/3/snapshots/1/1` und `/loadQuickWorkspace` (1 bis 30). Brauchen wir
nicht mehr; es bleibt die Rueckfallebene fuer aeltere TotalMix-Versionen.

## 3. Wir haben den halben Weg schon gebaut

Rea-Sixty spricht **kein** OSC. Es gibt keinen einzigen OSC-Aufruf im Code.
Was es gibt, ist der RME-Modus vom 19.08.2026: Rea-Sixty schickt **benannte
REAPER-Actions** an TotalReaper und liest Werte ueber `P_EXT:totalreaper_*`
auf der Spur zurueck (`main.cpp:1640` ff., `main.cpp:1830` ff.).

TotalReaper dagegen hat alles, was fehlt, fertig und in C++:
`src/osc/OscClient.cpp`, `OscServer.cpp`, `OscMessage.cpp`, `TotalMixState.cpp`,
inklusive Bundle-Parsing und einer Dump-Action. Das ist Franks eigener Code,
gleiche Sprache, gleiches Build-System.

Am 19.08. wurde eine Integration verworfen, Zitat aus
`docs/session-2026-08-19-uf1-rec-rme.md`: "a double install would put two OSC
clients on one port". Dieser Einwand ist mit Global OSC entschaerft, denn dort
sind mehrere Clients auf getrennten Portpaaren vorgesehen. Er faellt aber nicht
weg, er wird zu einer Einstellung: wer zwei Clients hat, muss zwei Portpaare
konfigurieren koennen.

### Drei Wege, einer davon empfohlen

**A) Ueber TotalReaper, wie heute.** Jede Side-Car-Funktion wird eine neue
benannte Action in TotalReaper, Rea-Sixty dispatcht sie. Kein zweiter Socket,
kein Portproblem. Bricht bei allem, was einen **Wert** statt eines Tritts
braucht: der Fader auf Main Volume ist absolut, und der Pegelstrom ist ein
Strom. Actions und `P_EXT` sind dafuer der falsche Kanal.

**B) Eigener Global-OSC-Client in Rea-Sixty, eigenes Portpaar.** Der
OSC-Code aus TotalReaper wandert als Modul herueber (oder wird geteilt).
Rea-Sixty bekommt einen `RmeOscClient` neben `HueClient`, `ObsManager` und
`DynaMountManager`, also genau das Muster, das dort dreimal steht: eigener
Worker-Thread, Zustand in Atomics, Anwendung im `onTimerBody_`-Tick.
Volle Kontrolle ueber Pegel, Werte und Rueckmeldung. Der Preis ist die
Doppelung: wer TotalReaper auch nutzt, faehrt zwei Clients.

**C) Beides.** B als Basis, und TotalReaper bleibt, was es ist. Die beiden
reden nicht miteinander, sondern jeder mit TotalMix, auf getrennten Ports.
Das ist laut RME genau der vorgesehene Betrieb.

Empfehlung: **B/C.** Der Fader und die Pegel sind der Grund, warum A nicht
reicht, und beide sind der Kern der Idee.

## 4. Side-Car im UF1-Code: wo es einhaengt

Die Architektur traegt das, es ist kein Umbau. Drei Stellen.

**Der Bildschirm.** `uf1PaintChannel_` (`main.cpp:32745`) hat oben bereits
eine Uebernahme: Hue Mode steigt dort aus und malt alles selbst
(`main.cpp:32764` ff.). Ein Side-Car-Maler ist derselbe Griff. Die Regeln
dafuer stehen in der Memory `uf1-screen-owning-mode-checklist`: wer oben
aussteigt, uebernimmt Soft-Key-Beschriftungen, deren vier LEDs, die
Hervorhebungsmaske, Name/dB/Wertzeile/Kanalnummer/Farbbalken, V-Pot-Labels
und -Balken, den EQ-Graph und den Fader. Und die Modusflanke muss
`g_uf1Gen` hochzaehlen, sonst haengt der alte Inhalt auf dem Glas.

**Der Fader.** Der schreibende Pfad ist schon eine Besitzerkette
(`main.cpp:34963` ff.): `stripFader`, `extSendFader`, `sendFader`,
`stickyFader`, `flipParamFader`, `flipPanFader`, sonst Spurlautstaerke.
Side-Car ist ein weiterer Zweig weit oben. **Achtung, zweite Kopie:** die
dB-Anzeige ueber dem Fader hat dieselbe Kette noch einmal (`faderDb`,
`main.cpp:34940` ff.). Genau diese Klasse Fehler war der 16.09.: eine
Entscheidung, zwei Kopien, auseinandergelaufen.

**Der Einstieg.** Hue Mode wird von einem Builtin getoggelt
(`main.cpp:52999`). Die Jog-Modi (`Uf1JogMode`, `main.cpp:5762`: Playhead,
Scrub, Items, Envelope, Razor, Fades) zeigen das Muster fuer eine Liste von
Modi mit Sichtbarkeitsschaltern in den Settings und eigener Nav-Belegung pro
Modus (`kUf1JogModeCountForNav`, `Bindings.h:288`).

Side-Car ist damit die Jog-Mode-Idee eine Ebene hoeher: nicht "was macht das
Rad", sondern "wem gehoert die ganze Flaeche". Die Definition, die Frank
vorschlaegt, traegt: Side-Car ist jeder Modus, der den Fader nicht mehr fuer
eine REAPER-Spur braucht. Das ergibt eine Kategorie mit mindestens drei
Bewohnern: RME Monitor, Item-Volume, Zoom/Navigation.

### Vorschlag fuer die Belegung RME Monitor

Die Hardware, belegt in `UF1Protocol.h:53` ff.:

| Control | Id | Vorschlag |
|---|---|---|
| Fader (motorisiert, Touch, 15 Bit) | - | Main Out Volume, absolut, Motor folgt der Rueckmeldung von TotalMix |
| Jog (nur Drehung) | `enc::kJog` 0x06 | Main Out Volume in Schritten, das ist die ARC-Geste |
| V-Pot 1-4 | 0x01-0x04 | vier Ausgaenge nach Wahl, Phones zuerst, mit Namen und dB in der Wertzeile |
| V-Pot ueber dem Fader | 0x00 | Cue-Ziel oder Talkback-Level |
| V-Pot-Pushes | 0x09-0x0C | Mute des jeweiligen Ausgangs |
| 4 Display-Soft-Keys | 0x19-0x1C | bankbar: Dim, Mono, Speaker B, Talkback, Cue, Snapshots, Layouts |
| Kanal-Encoder | 0x05 | Ausgang waehlen, Push bestaetigt |
| Solo / Cut / Sel | 0x1D-0x1F | frei, bieten sich fuer Dim / Mono / Talkback an |

Das schlaegt die ARC in zwei Punkten, die kein Umweg sind, sondern direkt aus
der Hardware fallen: ein **motorisierter** Main-Volume-Fader, der einem
Snapshot-Wechsel folgt, und **Beschriftung mit Pegelwert** an jedem Regler.
Die ARC hat beides nicht.

## 5. Und der Rest der Kategorie

Item-Volume auf den Fader und Zoom auf Jog/Fader brauchen kein OSC und keinen
RME. Sie sind dieselbe Uebernahme mit anderem Inhalt und koennen zuerst
gebaut werden, wenn das Geruest beweisen soll, dass es traegt.

## 6. Konfiguration, fuer beide Produkte

Die Leitfrage ist nicht "welches Format", sondern **wo eine Entscheidung
genau einmal steht**. Drei Entscheidungen, drei Orte.

### 6.1 Welche Taste macht was: das gibt es schon

RME-Funktionen werden **Builtins**, wie jede andere Aktion in diesem Projekt
auch (`registerBuiltin`, `Bindings.h:866`). Dann erbt jede Flaeche sie in dem
Moment, in dem sie registriert sind, ohne eine Zeile Bindungscode:

- die UF1-Bankmatrix, 10 Baenke mal 4 Display-Soft-Keys mal Plain/Shift
  (`kUf1SoftBankCount = 10`, `kSoftKeyModifierSets = 2`, `Bindings.h:762`)
- die UF8-Quicks, die UC1-Tasten, Long-Press, Doppelklick, die vier
  Modifier-Slots
- die Stream-Deck-Bruecke, die schon alles dispatcht, was ein Builtin ist

Der Katalog, erster Entwurf:

```
rme_dim              rme_mono            rme_speaker_b       rme_link_ab
rme_talkback         rme_mute_fx         rme_ext_in          rme_recall
rme_cue <bus>        rme_snapshot <n>    rme_layout <n>      rme_show_window
rme_global_mute      rme_global_solo     rme_undo            rme_redo
rme_main_vol <delta> rme_out_vol <rolle> <delta>
rme_durec_play       rme_durec_record    rme_durec_stop
```

Alles, was einen Zustand hat, bekommt `toggleaction`, sonst zeigt REAPERs
Menue keine Lampe ([[reaper-actions-need-toggleaction]]).

### 6.2 Welche Kanalnummer ist was: `rme.json`

Das gehoert **nicht** in eine Tastenbelegung. Stuende die Kanalnummer in der
Bindung, stuende sie in fuenfzig Zellen, und der Tag, an dem ein Interface
getauscht wird, waere ein Suchen-und-Ersetzen-Tag.

Also eine eigene, kleine Datei mit den **Rollen**:

```json
{
  "connection": { "host": "127.0.0.1", "send": 7008, "receive": 9008 },
  "roles": { "main": 0, "mainB": 6, "phones": [8, 10, 12, 14], "talk": 2 },
  "sources": { "show": "active", "hidden": "skip" },
  "steps":   { "mainVolumeDb": 0.5, "phonesDb": 1.0 }
}
```

Das Builtin heisst dann `rme_out_vol phones1 +1`, nicht `rme_out_vol 8 +1`.
Und die Rollen muss niemand tippen: TotalMix sagt sie selbst ueber
`/controlroom/mainout`, `/mainoutb`, `/phones1` bis `/phones4`, `/talkchannel`.
Die Datei ist damit eher ein Cache als eine Konfiguration.

### 6.3 Zwei Produkte, eine Datei

`bindings.json` ist bereits nach stabilen snake_case-ButtonIds geschluesselt,
und ein `ActionStep` traegt bereits seinen **Typ** (REAPER-Aktion, Tastenakkord,
Builtin, MIDI). Damit liest das Standalone dieselbe Datei und **ueberspringt,
was es nicht kennt**. Kein zweites Format, kein Konverter, kein Export-Schritt,
der vergessen werden kann.

Was das Standalone trotzdem braucht, ist ein **eigener kleiner Editor** fuer
dieselben zwei Dateien. Ein Produkt, dessen Konfigurator ein anderes Produkt
ist, ist kein Produkt. Gleicher Parser, gleiche Datei, eigene Oberflaeche.

### 6.4 Soft-Key-Baenke: statisch und dynamisch

**Statisch** ist eine Werksbank in der 10x4-Matrix, genau wie die Focus-Set-
und Plug-in-Ops-Baenke heute: Dim, Mono, Speaker B, Talkback auf Bank n,
Ext In, Mute FX, Recall, Cue auf Bank n+1.

**Dynamisch** ist alles, wo TotalMix die Liste besitzt und nicht wir. Dafuer
gibt es `DynamicBankKind` (`Bindings.h:627`), und der Praezedenzfall ist
`ObsScenes`: eine fremde Liste wird zur Reihe, mit eigenen Beschriftungen und
eigenen Lampen. Vier Einhaengepunkte, alle bekannt:

| Ort | Was | main.cpp |
|---|---|---|
| Aufloeser | Beschriftung, LED, Farbe pro Slot | 7738 |
| Zaehler | wie viele Slots, fuer die Seitenzahl | 7905 |
| UF8-Druck | was ein Druck tut | 8214 |
| UF1-Druck | dasselbe fuer die Display-Keys | 8264 |

Dazu die zwei Namensfunktionen (31059, 31145) und
**`kDynamicBankKindLast` in `Bindings.h`**, sonst nimmt der Editor die neue Art
an, schreibt sie auf die Platte und der naechste Ladevorgang wirft sie
kommentarlos weg.

Drei neue Arten:

- **`RmeSnapshots`**: acht Slots, und die Lampe kann hier mehr als an/aus.
  `/snapshot/load/<n>` meldet zurueck **0 aus, 2 aktiv, 3 geaendert**. Also
  drei Farben, und "geaendert, nicht gespeichert" ist als Zustand sichtbar.
- **`RmeLayouts`**: `/layout/load/<n>`. Nur senden, also Lampe nur als
  Quittung.
- **`RmeOutputs`**: die Ausgaenge aus `rme.json` plus die, die TotalMix
  meldet. Diese Bank waehlt das **Ziel** fuer den Submix-Modus unten, und ihre
  Lampe zeigt, welcher Bus gerade der Submix ist.

## 7. Der Fader auf einem Matrix-Knoten: Submix View nachbauen

Kurz: ja, und es ist die bessere Idee als der Monitor-Controller, weil sie der
Form der UF1 entspricht statt der Form der ARC.

Die Pfade stehen in der Tabelle:

```
/mix/in/<n>/<bus>/faderlin    f   0..1, Fader-Kurve
/mix/in/<n>/<bus>/fader       f   dasselbe in dB, -300 = aus
/mix/in/<n>/<bus>/balpan      f   -1..+1
/mix/in/<n>/<bus>/solo        f
/mix/in/<n>/<bus>/groupflags  f   Bit 1..4 Mute, 6..9 Solo, 12..16 Fader
/mix/pb/<n>/<bus>/...             dasselbe fuer Software-Playback
```

`faderlin` ist 0 bis 1 auf derselben Kurve wie der UF1-Fader. Quelle waehlen,
Ziel waehlen, Fader anfassen, fertig.

### Die Belegung

| Control | Aufgabe |
|---|---|
| Kanal-Encoder | laeuft durch die Quellen: erst Inputs, dann Playbacks |
| Kanal-Encoder Push | Quelle als fokussiert setzen |
| Fader | `faderlin` des Knotens Quelle nach Ziel |
| V-Pot ueber dem Fader | `balpan` desselben Knotens |
| V-Pot 1 bis 4 | vier Nachbarquellen in denselben Bus, also fuenf Regler auf einem Bild |
| Soft-Key-Bank `RmeOutputs` | das Ziel, also welcher Submix gerade gebaut wird |
| SOLO | `/mix/in/<n>/<bus>/solo` |
| Kleines LCD | Name aus `/input/<n>/name`, Pegel aus `/level/in/<n>`, dB im Wertfeld |

### Vier Dinge, die man dabei wissen muss

1. **Einen Mute pro Knoten gibt es nicht.** Die Tabelle kennt `mute` nur am
   Strip (`/input/<n>/mute`), nicht am Matrixknoten. CUT wuerde die Quelle
   also **ueberall** stummschalten, nicht nur in diesem Submix. Entweder so
   beschriften, oder CUT faehrt den Knoten auf -300 dB und merkt sich den
   Wert. Zweiteres ist naeher an dem, was jemand erwartet, der einen
   Kopfhoerermix baut.
2. **Nicht durch achtzig tote Kanaele blaettern.** `/sendsubmix/<submix>` mit
   Wert **2** laesst TotalMix nur die Knoten senden, deren Fader ueber -65 dB
   steht. Das ist die Liste, die man will: was in diesem Mix ueberhaupt
   vorkommt. Wert 1 holt alles, fuer den Fall, dass man etwas dazunehmen will.
3. **Versteckte Kanaele.** `/input/<n>/color` ist nur sendend und liefert
   **0 fuer versteckt**, sonst einen Farbindex. Das ist gleichzeitig der Filter
   (Kanal-Layout gilt laut Spezifikation auch fuer Global OSC) und die Quelle
   fuer den Farbbalken auf `0x0018`. Achtung: **Index, nicht RGB**, wir
   brauchen also die Palette von TotalMix.
4. **"Follow Submix" soll aus sein**, sagt die Spezifikation selbst. Das klingt
   nach Einschraenkung und ist ein Vorteil: unser Submix ist unsere eigene
   Auswahl, TotalMix' Bildschirm bleibt, wo er ist. Man baut am UF1 einen
   Kopfhoerermix, ohne die Ansicht am Rechner umzuschalten.

Offen ist genau eine Sache: ob `'submix'` in `/sendsubmix/<submix>` die
Ausgangskanalnummer meint oder eine laufende Nummer. Sagt ein Dump in einer
Minute.

## 8. Ansichten: zwei Maler, nicht fuenf

Frank, 19.09.: "eine Ansicht eher global (4 Nachbarkanaele auf V-Pots,
auswaehlen ueber Push) und eine auf den ausgewaehlten Channel mit EQ,
Dynamics, Gain, aehnlich RME Rec Mode. Aber nichts doppeln."

Der Schluss daraus ist schaerfer, als er klingt. Eine Ansicht kann nur zwei
Dinge sein: **viele Kanaele mit je einem Regler**, oder **ein Kanal mit vielen
Reglern**. Alles, was wir bisher aufgeschrieben haben, faellt in eines der
beiden. Also nicht fuenf Ansichten, sondern **zwei Maler und ein Geltungs-
bereich**.

### 8.1 SPREAD: viele Kanaele, ein Parameter

Ein Maler. Was er zeigt, sagt der Geltungsbereich:

| Geltungsbereich | Kanaele | Parameter | ergibt |
|---|---|---|---|
| `controlroom` | Main, Main B, Phones 1-4 | `volume` | den Monitor-Controller, also die ARC |
| `outputs` | alle Ausgaenge | `volume` | Ausgangspegel allgemein |
| `submix <bus>` | Inputs + Playbacks | `/mix/.../faderlin` | die Submix View |
| `inputs` | Hardware-Eingaenge | `gain` | vier Preamps nebeneinander |

Vier V-Pots gleich vier Nachbarkanaele, Push waehlt, Kanal-Encoder schiebt das
Fenster. Der Monitor-Controller ist damit **keine eigene Ansicht mehr**,
sondern SPREAD mit dem Geltungsbereich `controlroom`. Das ist die Doppelung,
die wir uns sparen.

### 8.2 STRIP: ein Kanal, viele Parameter

Der zweite Maler, geblaettert wie Plugin Mode heute:

| Seite | V-Pot 1 | V-Pot 2 | V-Pot 3 | V-Pot 4 |
|---|---|---|---|---|
| Preamp | `gain` | `reflevel` | `width` | `balpan` |
| Low Cut | `lowcut/freq` | `lowcut/slope` | | |
| EQ 1 | `eq/band1gain` | `eq/band1freq` | `eq/band1q` | `eq/band1type` |
| EQ 2 | `eq/band2gain` | `eq/band2freq` | `eq/band2q` | |
| EQ 3 | `eq/band3gain` | `eq/band3freq` | `eq/band3q` | `eq/band3type` |
| Dyn | `compthres` | `compratio` | `attack` | `release` |
| Exp | `expthres` | `expratio` | `dynamics/gain` | |
| AutoLevel | `maxgain` | `headroom` | `risetime` | |

Die Schalter (`48v`, `pad`, `phase`, `msproc`, `instrument`, `autoset`,
`lowcut/enable`, `eq/enable`, `dynamics/enable`, `autolevel/enable`) gehoeren
auf Soft-Keys, nicht auf Pots. Auf Ausgaengen tauscht dieselbe Ansicht die
Seiten gegen Room EQ (9 Baender), `crossfeed`, `delay`, `loopback`,
`talkbacksel`.

### 8.3 Eine Auswahl, zwei Maler

SPREADs V-Pot-Push setzt den Kanal, den STRIP zeigt. Beide lesen dieselbe
Variable. Damit ist Franks "auswaehlen ueber Push" nicht eine Geste in einer
Ansicht, sondern die Bruecke zwischen beiden.

Und **der Fader bleibt in JEDER Ansicht dasselbe**: der Pegel des gewaehlten
Kanals. Im Submix-Geltungsbereich ist das der Knoten, sonst der Ausgangs- oder
Eingangspegel. Ein Fader, der je nach Seite etwas anderes tut, ist auf einem
Ein-Fader-Geraet kein Feature, sondern ein Ratespiel.

### 8.4 Was ein Side-Car ist, und was nur eine Ansicht ist

Frank, 19.09.: "Hue und DynaMount sind eigentlich UF8-Modes, die einfach eine
UF1-Ansicht haben. Richtiger Side-Car ist UF1 only."

Der Code gibt ihm recht, und zwar nachpruefbar: `g_uf1HueMode` kommt in der
Fader-Besitzerkette (`main.cpp:34940` bis `35060`) **nicht vor**. Hue Mode
nimmt den Bildschirm, nicht den Fader. Der Fader macht waehrenddessen weiter
REAPER.

Daraus die scharfe Fassung, die beide Antworten traegt:

> **Side-Car ist eine Eigenschaft des Paares aus Flaeche und Modus, nicht des
> Modus.** Ein Modus ist auf einer Flaeche ein Side-Car, wenn er DEREN Fader
> von REAPER wegnimmt. Nimmt er nur den Bildschirm, ist er dort eine Ansicht.

Damit ist Hue auf dem UF1 eine Ansicht (Bildschirm ja, Fader nein), TotalMix
auf dem UF1 ein Side-Car, und TotalMix auf dem UF8 ebenfalls eines, weil es
dort acht Fader nimmt. Hue und DynaMount bleiben, wo sie sind, und werden
nicht angefasst.

### 8.5 Der Einstieg: Shift und MODE halten

`kUf1ViewPlugin / Daw / Meter / Sends` (`main.cpp:1149`) bleibt unveraendert,
inklusive des static_assert gegen `kUf1ViewCountForKeys` -- ein Bump waere ein
Config-Upgrade ohne Gegenwert. Der MODE-Halten-Picker ist mit diesen vier
**voll**.

Also eine zweite Seite auf denselben vier Keys:

```
MODE halten            Plugin | DAW | Meter | Sends          (unveraendert)
Shift + MODE halten    Side-Car-Seite, vier pro Seite
  < und >              blaettern, solange gehalten wird
```

`<` und `>` sind `kBankLeft` (0x21) und `kBankRight` (0x23). Das Idiom gibt es
auf diesem Geraet schon zweimal: MODE halten plus Soft-Key, Scrub halten plus
Jog. Vier Plaetze reichen heute (TotalMix, Item-Volume, Zoom, frei), das
Blaettern kostet jetzt fast nichts und spaeter eine Umbaurunde.

### 8.6 ⛔ Die V-Pot-Reihe wird trotzdem zuerst aufgeraeumt

Das Ergebnis aus 8.5 aendert daran nichts. Die Doppelung ist keine Frage der
Modus-Kategorie, sondern eine Frage, **wer eine physische Reihe beschreibt**:
`uf1PaintChannel_` (`main.cpp:33803`) und `uf1PaintHue_` (`main.cpp:32448`)
schreiben beide `0x010e` / `0x010f` / `0x010d`. Hue bleibt eine Ansicht und
bleibt trotzdem der zweite Schreiber. SPREAD und STRIP waeren der dritte und
vierte.

## 9. Der EQ-Graph: ja, und er wird dabei besser

`uf1PaintEqGraph_` (`main.cpp:28690`) besteht schon aus zwei Haelften, sie
sind nur nicht getrennt:

1. **Sammeln.** 15 Parameter-Indizes gegen den aufgeloesten FX, gelesen mit
   `uf1ParamFmt_` / `uf1ParamFreq_`, plus die Sonderfaelle (ReaEQ zaehlt in
   Oktaven, 32C hat keine Q, EQ In kann invertiert sein).
2. **Rechnen und senden.** `uf1PeakDb_`, `uf1HighShelfDb_`, `uf1LowShelfDb_`,
   `uf1HpfDb_`, `uf1LpfDb_`, aufsummiert ueber 249 Spalten von 20 Hz bis
   20 kHz logarithmisch, `dbToH`, zwei Frames auf `0x0122`.

Teil 2 weiss von REAPER nichts. Ein RME-Kanal braucht also **keinen zweiten
Graph**, sondern einen zweiten Sammler, der dieselbe Struktur fuellt.

### 9.1 Die Struktur gehoert verallgemeinert

Heute sind die Baender fest: HF, HMF, LMF, LF, HPF, LPF. Die RME-Eingaenge
haben 3 Baender plus Low Cut, die Ausgaenge haben **Room EQ mit 9 Baendern**.
Also aus den festen Slots eine Liste machen:

```cpp
struct EqBand { enum Kind { Bell, LowShelf, HighShelf, HighPass, LowPass } kind;
                double freq, gainDb, q; };
struct EqModel { bool on; std::vector<EqBand> bands; };
```

Die Renderschleife summiert dann ueber `bands` statt ueber sechs Namen. Das
ist dieselbe Mathematik, ein paar Zeilen kuerzer, und der 9-Band-Room-EQ faellt
gratis ab.

### 9.2 ⛔ Die Falle, die hier schon einmal zugeschlagen hat

Der Sammler ist voll mit Klemmen, und die stehen dort nicht aus Vorsicht,
sondern weil eine ungeklemmte Filterfrequenz den **ganzen** Graph auf den
Boden reisst ([[uf1-eq-graph-hplp-slam-trap]]): HPF ausserhalb 9 bis 2000 Hz
gilt als aus, LPF ausserhalb 1000 bis 30000 Hz gilt als aus. Dieselben Klemmen
gelten fuer den RME-Sammler, und zwar bevor die erste Zahl aus TotalMix
kommt, denn die Spezifikation sagt bei `eq/band1freq` nur `f`, ohne Bereich.
Ob das Hertz sind oder 0..1 wie im Legacy-Protokoll, ist **ungemessen**. Ein
Dump beantwortet es, und bis dahin wird nichts gezeichnet.

Dazu die zweite Regel aus derselben Funktion, die hier genauso gilt: **"ein
leerer Graph auf dem richtigen Kanal ist richtig, eine Kurve vom falschen
Kanal nie"**. Im Side-Car ist das leichter als heute, weil der Modus den
Bildschirm besitzt: es gibt keinen Wettbewerb darum, welcher FX gemeint ist,
der Kanal steht fest.

### 9.3 Was NICHT geteilt wird

Der Versuch, einen RME-Kanal als Pseudo-Plug-in in `PluginMap` zu stecken,
waere die falsche Sparsamkeit. Diese Maschinerie existiert, um die
**numerierten** Parameter eines fremden VST auf benannte SSL-Slots abzubilden:
Domain, linkIdx, FX Learn, Favourites, Learn-HUD. Ein RME-Kanal hat dieses
Problem nicht, seine Parameter sind in der Spezifikation benannt und getypt.
Es gibt nichts zu lernen.

Die Trennlinie also: **die Maler werden geteilt, das Parametermodell nicht.**
Der RME-Teil bringt eine eigene, statische Tabelle mit (Pfad-Blatt,
Beschriftung, Kurzname, Einheit, Bereich, Schrittweite, Format) und schickt sie
durch `uf1EmitVpotRow_`, die Wertzeile und den EQ-Renderer.

## 10. TotalMix auf dem UF8

Frank will die Ansicht dort mitziehen, und das ist die richtige Flaeche dafuer:
acht Strips, acht Fader, acht Scribble-Zeilen, acht Farbbalken. SPREAD ist auf
dem UF8 kein Kompromiss, sondern der Normalfall -- acht Kanaele statt vier,
ohne Blaettern.

### 10.1 Es gibt dafuer schon eine Achse: `SelectionMode`

Erster Befund war falsch und ist hier korrigiert, weil er die ganze Empfehlung
gedreht haette. `uf8::dynamount::manager().mountForStrip(strip)` steht an acht
Stellen, und das sieht nach "pro Strip gebaut" aus. Liest man die Bedingung
drumherum statt nur den Treffer, steht davor ueberall dasselbe:

```cpp
if (g_selectionMode.load() == SelectionMode::DynaMount) { ... }
```

Und der Enum lautet (`main.cpp`, bei `g_selectionMode`):

```cpp
enum class SelectionMode : uint8_t {
    Norm, Rec, RecMon, Auto, Instance, InstanceCycle, DynaMount, Hue };
```

Damit ist die Frage beantwortet, bevor wir sie gestellt haben: **die Achse
"der UF8 macht gerade etwas anderes als Spuren" existiert, sie heisst
`SelectionMode`, und Hue und DynaMount sind bereits Mitglieder.** Das
bestaetigt Franks Einordnung aus 8.4 ein zweites Mal, diesmal aus dem Code
statt aus dem Gefuehl.

TotalMix auf dem UF8 ist also ein **neues Mitglied dieses Enums**, mit
derselben Form: ein globaler Zustand, und darin eine Abbildung Strip auf
Kanal, so wie DynaMount Strip auf Stativ abbildet. Kein neues Konzept, kein
Parallelmechanismus, und DynaMount wird nicht angefasst.

Was dabei auffaellt und hier nur genannt, nicht gebaut wird: die Frage
`g_selectionMode == X` steht fuer DynaMount allein an sechs Stellen verteilt.
Ein weiteres Mitglied bringt seine eigenen sechs mit. Das ist die Bauart
dieses Codes und funktioniert; es waere nur der Moment, in dem eine gemeinsame
Aufloesung ("was adressiert Strip N") sich das erste Mal wirklich lohnt.

### 10.1.1 Wie man hineinkommt, und ob der UF8 mitlaeuft

Frank, 20.09.: *"Wie wuerde man auf den UF8 TotalMix mode kommen? Eine Option
ob das UF8 im Side-Car Mode pro mode mitlaeuft oder nicht?"*

Das Projekt hat die Frage schon einmal beantwortet, und zwar mit **zwei
unabhaengigen Schaltern**. Hue gibt es heute doppelt:

| | was es ist | wie man hinkommt |
|---|---|---|
| `SelectionMode::Hue` | die acht UF8-Strips werden acht Lampen | ein `selection_mode_*`-Builtin auf einer Taste |
| `g_uf1HueMode` | der UF1-Schirm wird der Lampen-Editor | das Builtin `uf1_hue` |

Sie sind **nicht gekoppelt**: man kann eines ohne das andere haben. Und das
UF1-Builtin traegt im Code den Grund mit: *"Bindable from ANY surface (not just
the UF1) ... the point of a lamp screen is to reach it from wherever your hand
already is."*

Daraus der Vorschlag, in drei Teilen:

1. **Der UF8 bekommt ein eigenes Mitglied** auf der bestehenden Achse,
   `SelectionMode::TotalMix`, mit einem Builtin wie die anderen. Damit ist es
   von jeder Flaeche aus erreichbar und kann auf jeder Taste liegen.
2. **Der UF1 behaelt Shift + MODE** und bekommt zusaetzlich sein Builtin, damit
   eine UF8-Taste ihn mitnehmen kann. Genau wie bei Hue.
3. **Die Kopplung ist eine Einstellung, nicht der Mechanismus.** Franks
   Instinkt ist richtig, und die Praezisierung ist sein eigenes "pro mode":
   "der UF8 laeuft mit" ergibt fuer **TotalMix** Sinn (UF8 = SPREAD, UF1 =
   STRIP, das ist ja der Gewinn) und fuer **Item-Volume** oder **Zoom**
   ueberhaupt nicht, das sind Ein-Fader-Ideen.

Also eine Spalte "UF8 laeuft mit" pro Side-Car-Modus, vorbelegt **an** fuer
TotalMix und **aus** fuer alles andere. Die Liste mit Schaltern pro Modus gibt
es als Muster schon: die Jog-Modi (`reasixty_uf1JogModeVisible` /
`setUf1JogModeVisible`).

⛔ **Und die Kopplung braucht eine Rueckfahrkarte.** Wenn der UF1-Einstieg
`g_selectionMode` auf TotalMix stellt, muss der Ausstieg den vorherigen Modus
wiederherstellen — und der kann sich zwischendurch geaendert haben, weil
`selection_mode_*` auf jeder Taste liegen darf. "Vorherigen Zustand merken und
zurueckschreiben" ist in diesem Projekt schon mehrfach die Fehlerquelle
gewesen. Sicherer: der Ausstieg setzt auf **Norm**, wenn der UF8 beim Einstieg
mitgenommen wurde und seither niemand von Hand umgeschaltet hat; sonst laesst
er ihn stehen.

### 10.2 Was der UF8 kann, was der UF1 nicht kann

| UF8-Element | im TotalMix-Modus |
|---|---|
| 8 Fader | acht Kanalpegel, oder acht Knoten in denselben Bus |
| 8 V-Pots | `balpan` je Kanal, oder `gain` im Input-Bereich |
| Scribble oben / unten | Kanalname aus `/input/<n>/name`, Wert darunter |
| Farbbalken | `/input/<n>/color`, sobald wir die Palette haben |
| SEL / CUT / SOLO je Strip | Auswahl, `mute`, `solo` |
| Bank links / rechts | acht Kanaele weiter, ohne Blaettern im Kopf |
| Wertzeile | dB, wie ueberall sonst |

Die Kopplung UF1 plus UF8 ist dabei das Interessante: **UF8 = SPREAD,
UF1 = STRIP auf dem Kanal, den der UF8 ausgewaehlt hat.** Das ist genau die
Arbeitsteilung, die die beiden Geraete in REAPER schon haben, nur mit TotalMix
dahinter. Wer beide besitzt, bekommt einen Konsolenblick auf sein Interface,
den RME selbst nicht anbietet.

### 10.3 Und damit wird das Standalone ein anderes Produkt

Bisher hiess Standalone "ein UF1 als Monitor-Controller". Mit dem UF8 heisst
es "SSL-Flaechen als TotalMix-Konsole, ohne DAW". Das ist eine deutlich
groessere Geschichte und trifft Leute, die es heute nicht gibt: den UF8-
Besitzer mit Cubase, Logic oder Pro Tools.

Ein Haken gehoert dazu gesagt, und er ist groesser als beim UF1: **die Flaeche
gehoert immer nur einem.** Auf dem Mac heisst das, den UF8 in SSL 360 zu
deaktivieren, auf Windows den Treiberwechsel. In beiden Faellen verliert der
Cubase-Nutzer damit **SSL 360 als DAW-Steuerung**, also genau das, wofuer er
den UF8 gekauft hat. Ein Standalone, das nur TotalMix kann, ist fuer ihn kein
Tausch, den er macht.

Die Loesung waere, dass das Standalone **beides** kann: TotalMix ueber OSC und
DAW-Steuerung ueber MCU. Die zweite Haelfte existiert schon halb --
`MidiBridge` (`MidiBridge.h`) haelt zwei virtuelle Core-MIDI-Endpunkte und
uebersetzt MCU in beide Richtungen, heute fuer REAPER und heute nur macOS.
Das ist dann allerdings nicht mehr "eine duenne App", sondern ein Produkt mit
eigenem Umfang.

Entscheidung dazu offen und bewusst getrennt: Rea-Sixty zuerst, das Standalone
danach, und dann als eigene Planung.

## 11. Meter-View aus der RME-Hardware

Erstens eine Korrektur: **"Totalizer" heisst Totalyser**, und es ist kein
eigenes Produkt, sondern eine Ansicht in **DIGICheck**. DIGICheck ist ein
Empfaenger, kein Sender, ohne dokumentierte Schnittstelle nach aussen. Als
Quelle faellt es aus.

Die Quelle ist OSC, und die Frage ist seit der Spezifikation vom 21.07.2026
beantwortet:

```
/level/in/<channel>    f   Peak level [dB], only changing values sent
/level/pb/<channel>
/level/out/<channel>
```

Sendend, nicht empfangend, und mit Aenderungserkennung (die Changelog-Zeile vom
26.06. nennt ausdruecklich "reduced count of level messages due to improved
change detection"). Das ist genau der Strom, den die Pegelbalken und die
VU-Nadeln der UF1 brauchen, und er ist billiger als unser SSL-Meter-Protokoll,
weil Stille gar nichts kostet.

Was **nicht** geht: Goniometer, Korrelation und RTA. Aus einer Peak-Zahl pro
Kanal laesst sich kein Lissajous rechnen, dafuer braucht es die Samples. Die
UF1-Meter-View kann aus RME-Quellen also die Pegel und die Nadeln fuellen,
nicht die Grafik auf `0x0122`. Wer die will, muss das Audio selbst hoeren,
und im Standalone-Fall ist das sogar der naheliegende Weg (siehe 12.).

Beilaeufig faellt noch etwas ab: `/durec/time` und `/durec/state` kommen als
Text, und `/durec/play|pause|stop|record|next|previous` nehmen Befehle. Die UF1
hat eine Zeitzone (`0x0119`) und eine Transportreihe. Eine DuRec-Fernbedienung
auf derselben Flaeche kostet danach fast nichts.

## 12. Standalone, ohne DAW

### Koexistenz mit SSL 360

Der UF1 laesst sich in SSL 360 einzeln deaktivieren, und dann gibt SSL 360 ihn
frei, ohne dass der Rest der Flaechen stehenbleibt. Auf dem Mac ist das die
ganze Bedingung: libusb greift sich das freigegebene Geraet, SSL 360 fuehrt
UF8 und UC1 weiter. `docs/install-macos.md` sagt heute noch "SSL 360 und den
SSL360Core-Daemon beenden"; das ist die vorsichtige Fassung und gehoert
nachgezogen, sobald wir es selbst gesehen haben.

Auf **Windows** stimmt es so nicht, und das ist wichtig. Dort haengt der
Zugriff nicht an SSL 360, sondern am Treiber: WinUSB und SSLBUS schliessen
sich auf demselben Geraet aus. Schlimmer, unser Installer bindet in einer
Schleife **alle drei** Hardware-IDs um, die gerade angesteckt sind
(`main.cpp:51406`, PID 0021, 0023, 0025). Wer heute den UF1 auf WinUSB holt,
verliert damit auch UF8 und UC1 an SSL 360. Fuer ein Standalone, das nur den
UF1 will, braucht es eine Variante desselben Skripts mit **einer** ID. Das ist
eine kleine Aenderung an einer Schleife, aber sie muss gemacht werden, sonst
ist die Koexistenz auf Windows eine Behauptung.

### Was ein Standalone kostet

Die Drahtebene ist frei. `UF1Protocol.{h,cpp}` zieht nur `<cmath>`,
`UF1Device.{h,cpp}` nur libusb, `LogPath` und `uf1_init_sequence.inc`. Kein
REAPER-Header, kein WDL. Dazu der OSC-Code aus TotalReaper. Beides ist fertig
und gehoert Frank.

Teuer ist der Maler. Alles, was den UF1 heute etwas anzeigen laesst, steckt in
`main.cpp`: 263 Funktionsdefinitionen mit `uf1` im Namen, rund 2960 Zeilen, die
ihn erwaehnen. Diesen Block herauszuloesen ist ein Refactoring, kein Kopieren.

Die Rettung ist, dass ein Monitor-Controller den Block gar nicht braucht. Die
teure Haelfte ist die **Meter-View**: der Zyklus-Pacer, die Bildburst-Regeln,
das Fenster, in dem die Firmware rendert. Die Kanalansicht dagegen ist
geradeaus, und ihre kleinen Pegelbalken (`0x0009` / `0x000a`) laufen ohne
Pacer. Ein Standalone mit Kanalzone, Soft-Key-Reihe, V-Pot-Beschriftung,
Fadermotor und Pegelbalken ist deshalb ein **kleines** Programm. Eines mit
grosser Meter-View ist ein Port.

### Wie genau, konkret

Frank, 20.09.: *"wie genau machen wir es stand-alone?"*

**Ein Binary pro Plattform, kein REAPER.** Was es an Quellen mitnimmt, ist
heute schon frei von REAPER — drei der sechs sind es seit dem 19.09. geworden,
und das war der Zweck:

| Was | Haengt an | Rolle im Standalone |
|---|---|---|
| `UF1Protocol.{h,cpp}` | `<cmath>` | Rahmen bauen und lesen |
| `UF1Device.{h,cpp}` | libusb, `LogPath`, `uf1_init_sequence.inc` | Geraet, Keepalive, Worker |
| `RmeOsc.{h,cpp}` | nur std | Global-OSC-Codec |
| `RmeState.{h,cpp}` | nur std | Rollen, Kanaele, Snapshots, Pegel |
| `Uf1EqCurve.{h,cpp}` | nur std | der Graph, ueber eine Bandliste |
| `Bindings`-Parser | `bindings.json` | dieselbe Datei wie Rea-Sixty |

**Was es NICHT mitnimmt, ist der Maler.** Der lebt in `main.cpp` und redet mit
REAPER. Das Standalone bekommt einen eigenen, kleinen: Kanalzone,
Soft-Key-Reihe, V-Pot-Reihe, Fadermotor, Pegelbalken. **Keine Meter-View** —
die braucht den Zyklus-Pacer und die Burst-Regeln, und das ist ein Port, keine
Abzweigung.

**Die Oberflaeche ist die offene Frage, und sie hat eine Hausloesung.** Ein
Konfigurator muss dreimal gebaut werden, wenn er nativ ist. Frank hat fuer
genau diesen Mixer schon eine Bruecke, die ihre Oberflaeche im Browser
aufmacht (`stoerme`, HTTP auf :8088 neben dem OSC-Socket). Dasselbe Muster
hier heisst: das Standalone ist ein Dienst mit einer lokalen Seite, und die
Seite ist auf jeder Plattform dieselbe. Alternative waere headless mit reiner
Dateikonfiguration, was fuer ein Produkt zu wenig ist.

**Das Geraet bekommt man so:**

* macOS: den UF1 in SSL 360 einzeln deaktivieren, dann gibt SSL 360 ihn frei
  und fuehrt die anderen Flaechen weiter. (Noch nicht selbst gesehen, steht in
  13.7.)
* Windows: WinUSB, und dafuer braucht es die Installer-Schleife **pro Geraet**
  statt fuer alle drei Hardware-IDs (siehe oben in diesem Abschnitt).

**Und die Entscheidung, die alles andere bestimmt**, steht in 10.3: reicht ein
TotalMix-Controller, oder muss das Standalone auch DAW-Steuerung ueber MCU
koennen? Ohne die gibt ein UF8-Besitzer SSL 360 nicht auf. `MidiBridge` haelt
die Haelfte davon schon, heute nur fuer macOS.

### ⇨ ENTSCHIEDEN 20.09.: das Standalone ist ein UF1-Produkt

Frank: *"Ich glaube, die meisten Leute die alle 3 Geraete haben, nutzen
hauptsaechlich die UF8 und UC1 und fragen sich beim UF1 'wofuer hab ich das
eigentlich gekauft? Fader hab ich 8 auf dem UF8 und die Meter-View
rechtfertigt den Preis nicht. Teures Spielzeug.' (Hab ich schon oft so gehoert
von Usern.) Wir koennten die Standalone mit Fokus auf UF1 bauen und dann ist
die UF1 halt einfach ein RME-ARC-Ersatz mit mehr Funktionen und Infos auf
einem Display."*

Das loest den Einwand aus 10.3 auf, und zwar vollstaendig — **weil der
Einwand nur den UF8 betraf.** "Die Flaeche gehoert immer nur einem" wiegt
schwer, wenn das Geraet die DAW-Steuerung ist, fuer die es gekauft wurde. Beim
UF1 wiegt es fast nichts, wenn er ohnehin herumliegt. Derselbe Satz ist beim
einen ein Ausschlusskriterium und beim anderen ein Argument dafuer.

Dazu wird die Zielgruppe schaerfer: nicht mehr "Leute ohne REAPER" (also
niemand, der heute Rea-Sixty kauft), sondern **"Leute, die schon einen UF1
haben und ihn nicht benutzen"**. Und wer Monitorsteuerung will, kauft sonst
eine ARC — das Standalone ersetzt also eine Anschaffung, die derselbe Mensch
sowieso erwaegt.

**Die eine Spannung, die man bewusst stehen lassen muss:** ein Standalone
nimmt den UF1 aus Rea-Sixty heraus. Wir haben ein Jahr damit verbracht, ihn in
REAPER nuetzlich zu machen, und jetzt gibt es ein zweites Programm, das ihn
sich holt. Aufgeloest wird das vom Side-Car selbst: **ein REAPER-Nutzer
bekommt TotalMix als Modus, ohne den UF1 herzugeben, ein Nicht-REAPER-Nutzer
bekommt dasselbe als eigenes Programm.** Zwei Schalen, ein Code. Sie
konkurrieren nicht, solange das so gebaut wird — und genau dafuer sind die
sechs Quellen aus der Tabelle oben REAPER-frei.

**Was dadurch vom Nice-to-have zur Bedingung wird:** der WinUSB-Installer
**pro Geraet**. Solange die Schleife alle drei Hardware-IDs umbindet
(`main.cpp:51406`), nimmt ein Windows-Nutzer, der nur den UF1 abgeben will,
auch UF8 und UC1 von SSL 360 weg — und damit genau das, was die ganze Praemisse
verspricht. Auf dem Mac reicht das Deaktivieren des einzelnen Geraets in
SSL 360.

**MCU / "Hybrid" wird geparkt, und der Grund ist richtig.** Frank: *"Fuer MCU
Mode muessten wir die ganzen SSL-360-Sachen die sie 'Hybrid' nennen mitnehmen,
also DAW-spezifische Settings die in SSL 360 sind."* Das ist kein Feature,
sondern ein Produkt: pro DAW eigene Belegungen und Verhalten. Gehoert notiert
und nicht angefangen.

⇨ **Eine Korrektur zur Reihenfolge**, weil sie die Begruendung betrifft und
nicht das Ergebnis: der UF8 haengt **nicht an der Nachfrage**, sondern an
genau dieser MCU-Entscheidung. Ein UF8-Besitzer gibt SSL 360 nicht fuer einen
reinen TotalMix-Controller auf, egal wie viele danach fragen. Die Nachfrage
sagt, **ob** es sich lohnt, die MCU-Frage ueberhaupt aufzumachen.

### Die Empfehlung zur Reihenfolge

Bauen, aber nicht jetzt und nicht als Zwilling.

1. Side-Car in Rea-Sixty zuerst. Dort entsteht der RME-Code sowieso, und dort
   hat er ein Zuhause mit Settings, Bindings und Handbuch.
2. Danach das Standalone als **duenne App** mit eigener, kleiner Anzeige:
   dieselbe Drahtebene, derselbe OSC-Client, ein eigener Maler, der nur die
   Kanalzone kann. Kein Plugin-Kram, keine Meter-View. Den **EQ-Graph kann es
   mitnehmen**, seit der Renderer am 19.09. eine eigene reine Datei geworden
   ist (`Uf1EqCurve`) — das war vorher ausgeschlossen und ist es nicht mehr.
3. Windows-Installer pro Geraet, sonst ist der Satz "laeuft neben SSL 360"
   auf der wichtigeren Haelfte der Nutzer falsch.

Was dagegen spricht, offen gesagt: es ist ein zweites Produkt mit eigenem
Build, eigener Signatur, eigenem Installer und eigenem Support, und es zielt
auf Leute, die kein REAPER haben, also auf niemanden, der heute Rea-Sixty
kauft. Es ist eine Tuer, kein Geschaeft. Als Tuer kann es gut sein: ein
kostenloses kleines Programm, das einen UF1-Besitzer ohne REAPER zum ersten
Mal etwas mit seinem Geraet machen laesst, das SSL nicht vorgesehen hat.

## 13. Gemessen am 19.09. auf Franks Mac Studio

Nicht mehr offen, sondern gedumpt. Fireface UFX+ (23802132), TotalMix laeuft,
OSC Remote 1 (eigener Port 7001, Rueckweg 7002, Host localhost) war frei und
antwortet auf `/sendall` mit **3595 Adressen**. Nichts an TotalMix wurde dabei
veraendert; `/sendall` ist die Anfrage, die Franks eigene stoerme-Bruecke beim
Start auch schickt.

### 13.1 Die Einheiten, der Blocker fuer den EQ-Graph: **echte Werte**

```
/input/0/eq/band1freq   80.0        Hz, kein 0..1
/input/0/eq/band2freq   1000.0
/input/0/eq/band3freq   5000.0
/input/0/eq/band2q      1.0         echtes Q
/input/0/eq/band1gain   0.0         dB
/input/0/lowcut/freq    20.0        Hz
/input/0/lowcut/slope   1.0         Index
/input/0/eq/band1type   1.0         Index
/input/0/fxsend         -16.0       dB
/input/0/autolevel/maxgain 6.0      dB
```

Damit ist Abschnitt 9.2 erledigt: der Sammler kann direkt in `EqModel`
schreiben, ohne Skalenraten. Die Klemmen bleiben trotzdem drin.

Room EQ auf Ausgaengen genauso, neun Baender in Hz/dB/Q
(`/output/0/roomeq/band1freq 50.0`, `band1q 0.7`, ... bis `band9type`), und
jeder Wert kommt doppelt, links und rechts, wie die L/R-Spalte der Tabelle es
ansagt.

### 13.2 Die Rollen, ohne dass jemand eine Nummer tippt

```
/controlroom/mainout      0      /output/0  "Main"
/controlroom/mainoutb     6      /output/6  "Speaker B"
/controlroom/phones1      8      /output/8  "Phones 1"
/controlroom/phones2     10      /output/10 "Phones 2"
/controlroom/phones3      2      → NICHT in der Ausgangsliste (siehe 13.4)
/controlroom/phones4     -1      nicht belegt
/controlroom/talkchannel  8
/controlroom/linkab       1      A/B verkoppelt
/controlroom/dimreduction -20    dB
/controlroom/recallvolume -10    dB
/controlroom/extingain    -3     dB
/controlroom/cuechan      -1     kein Cue
```

`rme.json` aus 6.2 fuellt sich damit vollstaendig von selbst.

### 13.3 Snapshot-Zustand kommt tatsaechlich dreiwertig zurueck

```
/snapshot/load/1..8   0 0 0 0 3 0 0 0
```

Die 3 auf Slot 5 heisst "geaendert, nicht gespeichert", genau wie die Tabelle
sagt. Die dreifarbige Taste aus 6.4 hat also echte Daten hinter sich.

### 13.4 ⛔ Eine Rolle kann auf einen Kanal zeigen, den OSC nicht sieht

Sichtbar sind die Ausgaenge 0, 4, 6, 8, 10. `phones3` zeigt auf **2**, und den
gibt es in der Liste nicht. Das ist kein Auslassen im Bulk-Dump: `/sendchan/
output/2` bleibt **still**, waehrend `/sendchan/output/8` zur Kontrolle 73
Adressen zurueckgibt. Kanal 2 ist fuer diese Fernbedienung schlicht nicht da.

**Welche Einstellung ihn ausblendet, ist offen.** Erst hiess es hier "Hide in
OSC Remote 1" -- das war aus der Notiz in Franks stoerme-Konfiguration
abgeschrieben und nicht gemessen. Franks eigene Vermutung, das aktuelle
**Layout** blende ihn aus, passt besser zur Spezifikation ("Channel settings in
Channel Layout are effective for Global OSC too, hidden channels may be
received by option"), und `LastLayoutPrest` steht in der Geraetedatei auf 3.
Ein Beleg ist auch das nicht: die `OutputState`/`InputState`-Schluessel in
`Frame0` sehen zwar nach Sichtbarkeit aus (`OutputState2` ist 0, die sichtbaren
tragen 8576 oder 8613), aber `InputState0` ist ebenfalls 0 und Eingang 0 kommt
ueber OSC ganz normal. Die Schluessel bedeuten also etwas anderes.

Fuer den Bau ist die Ursache nachrangig, denn die Folge ist in beiden Faellen
dieselbe und sogar staerker als gedacht: **die sichtbare Menge ist nicht
konstant.** Sie haengt an einem Layout, das der Nutzer umschaltet. Und die
Rollen selbst haengen am Snapshot: in der Geraetedatei steht `Phones3Chan` als
`2, 78, 78, 2, 2, 2, 78, 78, 78` ueber die Snapshot-Slots, `Phones4Chan` als
`-1, 80, 80, 4, 4, -1, ...`.

Daraus zwei Regeln fuer den Code:

- `rme.json` ist ein **Cache, keine Konfiguration**. Die Rollen kommen laufend
  aus `/controlroom/...` und werden nachgezogen, wenn ein Snapshot laedt.
- Eine Rolle, die auf einen Kanal zeigt, den diese Fernbedienung nicht sieht,
  ist ein **Zustand**: die Kachel bleibt leer und sagt warum. Sie darf nicht
  auf einen Kanal schreiben, der fuer uns nicht existiert.

### 13.5 Der Umfang, mit dem wir rechnen

61 Eingaenge, 47 Playbacks, 5 sichtbare Ausgaenge, Bus-Indizes 0/4/6/8/10.
Preamp-Regler nur dort, wo es sie gibt: `gain` auf 28 Eingaengen, `48v` und
`autoset` auf 15. `phase` kommt 94 mal, also auch fuer die rechte Haelfte der
Stereopaare. Farben sind Indizes 0 bis 8, 0 heisst versteckt.

61 Quellen sind genau der Grund fuer `/sendsubmix/<bus>` mit Wert 2 aus 7.

### 13.6 Zwei Global-OSC-Clients gleichzeitig: ja

Waehrend dieser Messung lief Franks **stoerme**-Bruecke auf Remote 2 (7003
raus, 7004 rein) weiter und wurde von unserem Betrieb auf Remote 1 nicht
gestoert. Damit ist die Architekturfrage aus 3 beantwortet: Rea-Sixty bekommt
sein eigenes Portpaar und TotalReaper behaelt seines.

### 13.7 Zwei Dinge, die den Rest der Messung aufhalten

**Das Interface war aus.** Die Titelzeile von TotalMix sagt "Fireface UFX+
(23802132) - disconnected". Alles oben ist damit der **gespeicherte Zustand**,
den TotalMix offline vorhaelt: strukturell gueltig (Namen, Rollen, Einheiten,
Umfang), aber keine laufende Hardware. Pegel gibt es in diesem Zustand
grundsaetzlich nicht, und Options, Mixer Settings ist dabei ausgegraut, also
laesst sich auch der Haken "Send Peak Level" nicht setzen. Beides braucht ein
eingeschaltetes Interface.

**Die Version ist aelter als die Tabelle.** Installiert ist **TotalMix FX 2.10
alpha 8**, die Spezifikation ist "2.1 beta 2" vom 21.07.2026. Das erklaert
sauber, was im Dump fehlte: `/status/device|connection|dsp` kam nicht, und
`/sendstate` blieb wirkungslos -- beides steht im Changelog unter dem 21.07.
`/layout/load` (alpha 7) und `/input/<n>/color` (alpha 8) sind dagegen da.

⇨ Fuer den Bau heisst das: **gegen das pruefen, was laeuft, nicht gegen die
Tabelle.** Und eine Mindestversion nennen, sobald wir wissen, welche wir
brauchen.

## 14. Die UF1-Anzeige: was 2.1.12 brachte und was wir nicht fahren

Frank, 19.09.: "beim SSL-Update gabs doch neue Felder fuers UF1. Ich sah dort
mal einen neuen grossen Textbalken, wahrscheinlich in ihrem DAW-Mode, weil im
Plug-in-Mode dort der EQ-Graph ist. Und im gleichen Mode Farbbalken fuer die
vier zusaetzlichen Kanaele auf den V-Pots."

Aus dem vorhandenen Korpus beantwortet, ohne neuen Capture-Lauf.

### 14.1 Was 2.1.12 wirklich hinzugefuegt hat: ein Element

Init gegen Init, `cap129` (2.1.12) gegen `cap101` (davor):

```
nur in 2.1.12:   0x012b
nur in alt:      0x0125 0x0126 0x0127 0x0128
```

Also **genau eine neue Adresse**, und die ist schon bekannt: `0x012b` wurde im
Rahmen der roten VU-Zahlen am 11.09. probiert. Alles andere, was auf dem Glas
neu aussieht, ist **kein neues Feld, sondern neuer Inhalt in alten Feldern**.

### 14.2 ⛔ Korrektur: der grosse Textbalken ist NICHT unsere Kopfzeile

Erst stand hier, der Balken sei `0x011c`, unsere 8x25-Kopfzeile, und wir
faehren ihn laengst. Frank hat widersprochen, und er hat recht. `0x011c` ist
die Kopfzeile **in unserer Ebene**. Was er gesehen hat, liegt in einer anderen.

### 14.3 Die Ebene ist der Punkt, und sie hat einen Namen

`0x0100` ist der **Layout-Selektor des grossen LCD**, zwei Byte
`{layout, screen}`. Wir fahren genau zwei Werte:

```
{0x03, 0x00}          Kanal-Ebene (Plugin / DAW / Sends teilen sie sich)
{0x04, 0x00 .. 0x05}  Meter-Ebene, sechs Screens
```

Und der **gesamte Korpus** kennt nur diese beiden, plus ein einzelnes
`{0x80, 0x03}` ganz am Anfang des Kaltstarts (`cap101`, Frame 585, vor dem
ersten `{03,00}`). SSLs eigener **DAW-Layer** auf dem UF1 wurde nie
aufgezeichnet, also ist seine Layout-Nummer unbekannt. Das erste Byte nimmt
nachweislich mindestens 0x03, 0x04 und 0x80 an; der Raum ist groesser als die
zwei Ebenen, die wir benutzen.

Damit loest sich auch das Raetsel aus 14.1: die Felder sind nicht neu und auch
nicht versteckt, sie sind **hinter einer Ebene, die wir nie einschalten**.
Frank: "sind hinter dem EQ Graph."

### ⛔ 14.3.1 Und darum stimmt unsere eigene Notiz nur halb

In `UF1Protocol.h` steht "0x0100..0x011a render nothing at all, swept, blank".
Das ist eine Aussage **ueber die Ebene, in der gesweept wurde**, nicht ueber
die Elemente. Genau so gelesen habe ich sie, und genau daran bin ich in 14.2
gescheitert. Der Satz steht jetzt mit dieser Einschraenkung im Header.

**Leer in einer Ebene ist nicht leer.** Wer kuenftig schreibt, ein Element tue
nichts, sagt dazu, in welcher Ebene er das festgestellt hat.

### 14.4 Die vier Farbbalken: derselbe Grund

Gilt genauso. Es gibt keine Aufnahme von SSLs DAW-Layer, also auch keine von
dem, was er dort auf die V-Pots malt. Die vier Farb-Ids, die `cap132`
zusaetzlich bewegt (`0x09`, `0x0b`, `0x0c`, `0x0e`), sind es nicht: sie tragen
nur `0000f0`, `0011f1` und `00ffff` und bekommen daneben FF3B-Mono-Frames, das
sind Tasten-LEDs. Die bekannte Spurfarbe `0x07` traegt in derselben Aufnahme
sechs verschiedene Werte.

Was bleibt, sind die Vier-Byte-Elemente, die der Init beschreibt und die wir
nie anfassen. Vier Byte ist die Form einer Reihe pro V-Pot, `kVpotStyle`
(`0x010d`) ist auch vier Byte:

| Adresse | Init | |
|---|---|---|
| `0x0113` | `01 01 01 01` | vier gleiche Werte, wie ein Stil pro Pot |
| `0x0118` | `00 00 00 00` | |
| `0x0121` | `00 00 00 00` | sitzt zwischen Solo-Active `0x0120` und dem Graph `0x0122` |
| `0x012b` | `00 00 00 00` | das neue aus 2.1.12 |

Aber die Reihenfolge der Fragen hat sich umgedreht: **erst die Ebene, dann die
Elemente.** In `{03,00}` zeichnen sie nichts, und das war nie eine Aussage
ueber sie.

### 14.5 Der Versuch, den das erlaubt

Kein Capture-Lauf, und auch kein Blindschuss auf unbekannte Elemente. Der
Versuch ist **der Layout-Selektor, den wir laengst fahren**:

```
0x0100 <- {0x01,0x00} {0x02,0x00} {0x05,0x00} ...      und wieder {0x03,0x00}
```

Das ist mechanisch dieselbe Operation wie jeder MODE-Wechsel zwischen Kanal-
und Meter-Ebene, und **der Rueckweg ist der, den der Code ohnehin auf jeder
Flanke geht** (`put(0x0100, {0x03, 0x00})`). Damit ist das eine deutlich
zahmere Sonde als ein Schreibversuch auf ein unbekanntes Element: `0x011b` riss
seinerzeit das ganze Layout herunter, weil niemand wusste, was es ist. Hier
wissen wir es.

Findet sich eine Ebene, in der etwas anderes steht, dann erst die Elemente
darin abklopfen, und zwar mit den vier Kandidaten oben zuerst.

Zwei Bedingungen bleiben:

- ⛔ Eine unbekannte Ebene kann die Firmware in einen Zustand bringen, aus dem
  das Zurueckschreiben allein nicht reicht. Der Preis eines Treffers kann ein
  REAPER-Neustart sein, und die Sonde gehoert hinter einen Schalter.
- ⛔ **Frank ist farbenblind.** Eine Sonde, deren Ergebnis "welche Farbe
  erscheint" lautet, hat den falschen Ableser. Also unterscheidbare
  **Positionen** in die vier Byte schreiben (`00 01 02 03`), damit die Antwort
  "vier Balken, von links ansteigend" heisst und nicht "gruen, gelb, orange,
  rot".

## 15. Entschieden am 19.09.

Franks Antworten auf die sechs offenen Punkte, damit sie nicht noch einmal
aufgemacht werden:

1. **Keine Spurverfolgung.** Der REC/RME-Modus bleibt, was er ist, und nimmt
   weiter die REAPER-Spur. Im Side-Car gilt: **die UF1 IST TotalMix.** Eigene
   Auswahl, kein `I_RECINPUT`, keine Spur. Das ist ausserdem die Bedingung
   dafuer, dass derselbe Code im Standalone laeuft.
2. **Fader-Invariante:** ja. In jeder Ansicht der Pegel des gewaehlten Kanals.
3. **Einstieg Shift + MODE halten**, fuer alle Side-Car-Kategorien, mit `<`
   und `>` zum Blaettern (siehe 8.5).
4. **Hue und DynaMount bleiben, wo sie sind.** Sie sind UF8-Modi mit einer
   UF1-Ansicht, kein Side-Car. Der Code bestaetigt es: Hue kommt in der
   Fader-Besitzerkette nicht vor.
5. **Room EQ im Graph:** ja, neun Baender ueber den Band-Vektor.
6. **TotalMix kommt auf den UF8**, siehe 10. Und damit stellt sich die
   Standalone-Frage neu, siehe 10.3.

**Der Name.** Die Kategorie heisst Side-Car, dieses Mitglied heisst
**TotalMix**. Auf dem Geraet steht `TOTALMIX`, acht Zeichen, und acht ist genau
die gemessene Breite beider Displays ([[surface-text-field-widths]]). Im
Handbuch heisst es "TotalMix Mode", weil ein Nutzer, der auf sein Interface
schaut, dieses Wort liest und kein anderes.

## 16. Urteil und Reihenfolge

Machbar, ohne neue Erfindung: das Protokoll ist offen und vollstaendig
dokumentiert, der OSC-Code existiert in Franks Hand, die Pegel kommen mit, und
beide Flaechen haben fuer eine Uebernahme schon einen Praezedenzfall.

1. ✅ **Aufraeumen** (19.09.). `uf1EmitVpotRow_` ist der einzige Schreiber der
   V-Pot-Reihe, mit EINEM Cache statt zwei; die Bipolar-Regel, das
   Helligkeitsbyte und die Stil-Zuordnung liegen in `uf1VpotBar_`. Der EQ-Graph
   ist in `src/Uf1EqCurve.{h,cpp}` geteilt: Sammler bleibt in `main.cpp`,
   Renderer ist rein und nimmt eine **Bandliste** beliebiger Laenge. Die Kurven-
   mathematik ist woertlich uebernommen, nicht neu hergeleitet. Test:
   `tests/test_uf1_eq.cpp`.
2. ✅ **RME-Fundament** (19.09.). `src/RmeOsc.{h,cpp}` (Global-OSC-Codec inkl.
   Bundles), `src/RmeState.{h,cpp}` (Zustands-Cache, Rollen, Snapshots, Pegel),
   `tools/rme_osc_probe` gegen den echten Mixer. Tests:
   `tests/test_rme_osc.cpp`.
3. **Side-Car als Kategorie.** Zweite Ebene im Modusmodell, Shift + MODE als
   Einstieg, `<`/`>` zum Blaettern. Erster Bewohner: Zoom oder Item-Volume,
   damit das Geruest ohne OSC steht.
4. **Der Client im Tick.** Socket + Worker nach dem Muster von `HueManager` /
   `ObsManager`, eigenes Portpaar, `rme.json` als Rollen-Cache, die
   `rme_*`-Builtins, die drei dynamischen Baenke.
5. **SPREAD auf der UF1**, Geltungsbereich `controlroom`. Das ist die ARC.
6. **SPREAD `submix`** und **STRIP** mit EQ-Graph.
7. **Pegel** in die Kanalzone und in die Meter-View.
8. **TotalMix auf dem UF8** als globaler Modus, DynaMount unberuehrt.
9. **Standalone** als eigene Planung, mit der Frage aus 10.3 als erstem Punkt.

### 16.1 Zwei Fallen, die beim Bauen des Fundaments zugeschlagen haben

Beide sind gefixt und beide haben jetzt einen Test, aber sie gehoeren
aufgeschrieben, weil sie sich wiederholen werden.

**Ein rechter Kanal ist kein Kanal.** Parameter, die es pro Seite gibt (phase,
delay, gain, jedes Room-EQ-Band), werden auf der rechten Haelfte eines
Stereopaars mit Index + 1 adressiert. Die erste Fassung markierte einen Kanal
als vorhanden, sobald ueberhaupt etwas von ihm kam — und zaehlte damit **94
Ausgaenge, wo der Mixer 5 zeigt**. Schlimmer: die Rollenaufloesung haette einen
namenlosen Phantomkanal als sichtbar zurueckgegeben. Ein Strip meldet sich mit
einem **Namen**; danach stimmen 61 / 47 / 5 genau.

**Die Kurvenmathematik ist nicht "so ungefaehr eine Glocke".** Der erste
Entwurf von `Uf1EqCurve.cpp` ersetzte die kapturgefittete Peaking-Formel durch
eine Gausskurve, weil die auch wie eine Glocke aussieht. Beim Verschieben von
Code wird nichts neu hergeleitet, es wird kopiert.
