# Machbarkeitsstudie: UF1 Side-Car und RME Monitor Control

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

## 6. Meter-View aus der RME-Hardware

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
und im Standalone-Fall ist das sogar der naheliegende Weg (siehe 7.).

Beilaeufig faellt noch etwas ab: `/durec/time` und `/durec/state` kommen als
Text, und `/durec/play|pause|stop|record|next|previous` nehmen Befehle. Die UF1
hat eine Zeitzone (`0x0119`) und eine Transportreihe. Eine DuRec-Fernbedienung
auf derselben Flaeche kostet danach fast nichts.

## 7. UF1 standalone, ohne REAPER

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

### Die Empfehlung

Bauen, aber nicht jetzt und nicht als Zwilling.

1. Side-Car in Rea-Sixty zuerst. Dort entsteht der RME-Code sowieso, und dort
   hat er ein Zuhause mit Settings, Bindings und Handbuch.
2. Danach das Standalone als **duenne App** mit eigener, kleiner Anzeige:
   dieselbe Drahtebene, derselbe OSC-Client, ein eigener Maler, der nur die
   Kanalzone kann. Kein Plugin-Kram, kein EQ-Graph, keine Meter-View.
3. Windows-Installer pro Geraet, sonst ist der Satz "laeuft neben SSL 360"
   auf der wichtigeren Haelfte der Nutzer falsch.

Was dagegen spricht, offen gesagt: es ist ein zweites Produkt mit eigenem
Build, eigener Signatur, eigenem Installer und eigenem Support, und es zielt
auf Leute, die kein REAPER haben, also auf niemanden, der heute Rea-Sixty
kauft. Es ist eine Tuer, kein Geschaeft. Als Tuer kann es gut sein: ein
kostenloses kleines Programm, das einen UF1-Besitzer ohne REAPER zum ersten
Mal etwas mit seinem Geraet machen laesst, das SSL nicht vorgesehen hat.

## 8. Was noch gemessen werden muss

Nach der Spezifikation ist wenig uebrig, und alles davon liefert Franks
Dump-Action in TotalReaper.

1. Welcher `<n>` ist bei Frank Main, welche sind die vier Phones. Fragt sich
   selbst: `/controlroom/mainout`, `/phones1` bis `/phones4` sagen es.
2. Nimmt TotalMix einen zweiten Global-OSC-Client auf einem zweiten Portpaar
   an, waehrend TotalReaper auf dem ersten haengt.
3. Wie schnell `/level/out/<n>` bei Musik tatsaechlich kommt.
4. Ob der UF1 auf dem Mac wirklich freigegeben wird, wenn man ihn in SSL 360
   einzeln deaktiviert, ohne SSL 360 zu beenden.

## 9. Urteil

Machbar, und zwar ohne neue Erfindung: das Protokoll ist offen und jetzt auch
vollstaendig dokumentiert, der OSC-Code existiert bereits in Franks Hand, die
Pegel kommen mit, und der UF1-Code hat fuer eine Flaechenuebernahme schon den
Praezedenzfall. Der motorisierte Fader macht aus dem Nachbau der ARC etwas,
das die ARC nicht kann.

Reihenfolge:

1. Side-Car als Kategorie bauen, mit Item-Volume oder Zoom als erstem
   Bewohner. Kein OSC, nur das Geruest: Uebernahme, Fader-Zweig, dB-Zweig,
   Modusflanke, Settings-Eintrag.
2. RME Monitor als zweiter Bewohner, mit eigenem Global-OSC-Client auf
   eigenem Portpaar.
3. Pegel in die Kanalzone und in die Meter-View.
4. Standalone als duenne App, und der Windows-Installer pro Geraet.
