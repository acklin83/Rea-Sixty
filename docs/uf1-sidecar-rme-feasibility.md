# Machbarkeitsstudie: UF1 Side-Car und RME Monitor Control

Stand 2026-09-19. Alles hier ist gelesen, nicht erinnert: die RME OSC-Tabelle
ist heruntergeladen und ausgewertet, die Pfade von TotalReaper stammen aus
Franks eigenem Repo, die Rea-Sixty-Stellen sind mit Zeilennummern belegt.
Was offen ist, steht unten unter "Was gemessen werden muss".

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

### Was Frank will, und wo es liegt (Global OSC)

Belegt in `TotalReaper/docs/osc-paths-discovered.md`, alles am Geraet gedumpt:

| Wunsch | Pfad | Anmerkung |
|---|---|---|
| Main Out Lautstaerke | `/output/<n>/volume` | float in dB, -300 = -inf, 0 = oben. Main ist der konfigurierte Bus, es gibt KEIN `/controlroom/mainvolume` |
| Phones-Lautstaerken | `/output/<n>/volume` | PH 9/10 = n 8. Index = linker Kanal des Stereopaars |
| Dim | `/controlroom/dim` | 0/1 |
| Mono | `/controlroom/mainmono` | 0/1 |
| Speaker B | `/controlroom/speakerb` | 0/1 |
| Talkback | `/controlroom/talkback` | 0/1 |
| Cue | `/controlroom/cuechan` | -1 = aus, sonst Bus-Index |
| Snapshots | `/snapshot/load/<n>`, `/snapshot/save/<n>` | 8 Slots |
| Layouts | `layout` | laut RME-Forum in Alpha 7 dazugekommen, exakter Pfad bei uns noch nicht gedumpt |
| Mute / Solo pro Strip | `/input/<n>/mute`, `/mix/in/<n>/<bus>/solo` | schon in Benutzung |

Im Legacy-Protokoll heissen dieselben Dinge `/1/mainDim`, `/1/mainMono`,
`/1/mainSpeakerB`, `/1/mainTalkback`, `/1/mastervolume`, `/3/snapshots/8/1`
bis `/3/snapshots/1/1` und `/loadQuickWorkspace` (1 bis 30). Brauchen wir
voraussichtlich nicht, aber es ist die Rueckfallebene fuer Geraete oder
Versionen ohne Global OSC, und fuer die Pegel (siehe 6.).

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
Empfaenger, kein Sender. Es hat keine dokumentierte Schnittstelle, ueber die
wir Pegel abholen koennten. Als Quelle faellt es aus.

Die Quelle ist OSC. Im **Legacy**-Protokoll ist das belegt und
dokumentiert: `/1/level<N>Left` und `/1/level<N>Right`, Skalentyp
`kOSCScaleLevel`, dazu ein String in dB, und in den TotalMix-Einstellungen
gibt es dafuer den Schalter **"Send Peak Level"** samt Peak Hold. Pegel gehen
also grundsaetzlich ueber OSC raus.

Ob **Global OSC** dasselbe tut, ist der einzige echte Unbekannte dieser Studie.
In TotalReapers Dump-Liste steht bisher kein Pegelpfad, und die Forenrunde zur
Alpha sagt dazu nichts. Zwei Ausgaenge, beide gangbar:

- Global OSC kann es: ein Pfad, fertig.
- Global OSC kann es nicht: Legacy-Remote auf einem zweiten Portpaar nur
  fuer die Pegel. TotalMix erlaubt vier Legacy-Remotes, und die Bank-Mechanik
  ist fuer acht feste Ausgaenge kein Problem: einmal Bus und Bankstart setzen
  und liegen lassen.

Was **nicht** geht, unabhaengig davon: Goniometer, Korrelation und RTA. Aus
zwei Pegelzahlen laesst sich kein Lissajous rechnen, dafuer braucht es die
Samples. Die UF1-Meter-View kann also aus RME-Quellen die Pegelbalken und die
Nadeln fuellen, nicht aber die Grafik auf `0x0122`. Wer die will, muss das
Audio selbst hoeren, und das ist im Standalone-Fall (siehe 7.) sogar leicht:
eine Loopback-Spur ueber CoreAudio/WASAPI aufmachen und selber rechnen.

## 7. UF1 standalone, ohne REAPER

Die Drahtebene ist frei. `UF1Protocol.{h,cpp}` zieht nur `<cmath>`,
`UF1Device.{h,cpp}` nur libusb, `LogPath` und die Init-Sequenz
`uf1_init_sequence.inc`. Kein REAPER-Header, kein WDL, nichts, was ein
zweites Binary nicht auch haette. Ein kleines Programm mit Geraet, Keepalive,
OSC-Client und einem eigenen Maler ist auf dem Mac ein ueberschaubares Stueck.

Der teure Teil ist der Maler, nicht das Protokoll. Alles, was den UF1 heute
etwas anzeigen laesst, steckt in `main.cpp`: 263 Funktionsdefinitionen mit
`uf1` im Namen, rund 2960 Zeilen, die ihn erwaehnen, inklusive des
Zyklus-Pacers (`Uf1CycleParts`, `uf1EnsurePacer_`), ohne den die Meter-View
die Firmware in den Faulmodus schickt. Fuer einen **reinen Monitor-Controller**
braucht man davon wenig: Init, Keepalive, ein paar Bildschirmzonen,
Soft-Key-Beschriftung und LEDs, Fader-Motor, Encoder. Fuer eine **volle
Meter-View** braucht man den Pacer und die Burst-Regeln, und das ist ein Port,
keine Abzweigung.

Windows hat eine zusaetzliche Huerde, die es aber ohnehin schon hat: das
Geraet muss auf WinUSB umgebunden werden, und das nimmt es SSL 360 weg. Der
Installer dafuer steckt bereits in Rea-Sixty (`reasixty_installWinUsbDriver`,
`main.cpp:39067`) und waere im Standalone zu wiederholen.

Der ehrliche Satz dazu: standalone ist machbar und als Monitor-Controller
sogar klein. Es ist trotzdem ein **zweites Produkt** mit eigenem Build, eigener
Signatur, eigenem Installer und eigenem Support, und es konkurriert mit dem
Grund, Rea-Sixty zu installieren.

## 8. Was gemessen werden muss, bevor Code entsteht

Alles davon liefert Franks eigene Dump-Action in TotalReaper.

1. Sendet Global OSC Pegel? Wenn ja, unter welchem Pfad und wie oft.
2. Wie heisst der Layout-Pfad (Alpha 7)?
3. Welcher `<n>` ist bei Frank Main, welcher sind die vier Phones?
4. Nimmt TotalMix wirklich einen zweiten Global-OSC-Client auf einem zweiten
   Portpaar an, waehrend TotalReaper auf dem ersten haengt?
5. Meldet `/output/<n>/volume` seinen Wert bei fremder Aenderung zurueck? Der
   Fadermotor haengt daran.

## 9. Urteil

Machbar, und zwar ohne neue Erfindung: das Protokoll ist offen und
dokumentiert, der OSC-Code existiert bereits in Franks Hand, und der UF1-Code
hat fuer eine Flaechenuebernahme schon den Praezedenzfall. Der Fader macht aus
dem Nachbau der ARC etwas, das die ARC nicht kann.

Reihenfolge, die sich aus der Studie ergibt:

1. Messen (Abschnitt 8), eine Sitzung mit TotalMix und der Dump-Action.
2. Side-Car als Kategorie bauen, mit Item-Volume oder Zoom als erstem
   Bewohner. Kein OSC, nur das Geruest: Uebernahme, Fader-Zweig, dB-Zweig,
   Modusflanke, Settings-Eintrag.
3. RME Monitor als zweiter Bewohner, mit eigenem Global-OSC-Client.
4. Pegel in die Meter-View, sobald Frage 1 beantwortet ist.
5. Standalone erst danach, und als eigene Entscheidung.
