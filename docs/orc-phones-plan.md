# Plan: ORC MoreMe (stoerme in ORC, Kopfhörermixer für Musiker)

Stand 29.10.2026, Auftrag Frank: „Würd's schon gerne weitergeben“, Plan wie bei ORC-UF8.
Gebaut am 29.10.2026, Stufen 1-5, siehe 7.4. Am Gerät nicht geprüft.

## 1. Das Bild in fünf Sätzen

ORC bekommt einen Kopfhörermixer im Browser, so wie stoerme ihn heute als eigene Node-Bridge
anbietet. Jeder Musiker öffnet eine Webseite auf dem Handy, wählt seinen Kopfhörer (Phones 1-4)
und mischt sich seine Quellen selbst. ORC spricht dafür über eine **zweite TotalMix-Verbindung**,
die auch dann offen bleibt, wenn REAPER die Flächen übernimmt. Die Webseite ist stoermes Seite,
unverändert, und ORC spricht dasselbe JSON wie die Bridge. Wer ORC installiert, hat den Mixer
dabei, ohne Node und ohne LaunchAgent.

## 2. Was man sieht

**Der Musiker:** die stoerme-Seite, wie heute. PIN eingeben, Kopfhörer wählen, pro Quelle Fader,
Pan, Mute, Solo, dazu Master und Master-Mute. Ein Kopfhörer, den schon jemand benutzt, ist gesperrt
und lässt sich nur bewusst übernehmen (die „Tölpel-Sperre“).

**Der Nutzer in ORC:**
- Ein neuer Einstellungsreiter **Phones**: an/aus, Port der Webseite, PIN, TotalMix-Ports der
  zweiten Remote, Status.
- Im Menü eine Zeile **Phones** mit dem Zustand (aus, TotalMix antwortet nicht, Anzahl
  verbundener Handys) und der Adresse für die Musiker, dazu ein QR-Code zum Abscannen.
- Sichtbare Kanäle kuratiert man in TotalMix, wie bei stoerme: Options, Channel Layout, „Hide in
  OSC Remote N“ für die Remote des Mixers. Die Flächen (UF1, UF8) sehen weiter ihre eigene Auswahl.

## 3. Warum eine zweite TotalMix-Verbindung

| Grund | |
|---|---|
| **Übergabe** | Startet REAPER, schliesst ORC die Verbindung der Flächen (Remote 3), damit Rea-Sixty sie bekommt. Der Mixer muss gerade dann weiterlaufen, während der Aufnahme. Eine eigene Remote bleibt offen. |
| **Sichtbarkeit** | TotalMix kuratiert pro Remote. Musiker sehen andere Kanäle als die Flächen. |
| **Slots** | TotalMix hat vier Remotes. Belegt sind heute 1 TotalReaper (7001/7002, dessen README), 2 stoerme (7003/7004), 3 ORC/Rea-Sixty (7005/7006). ORC übernimmt den Slot von stoerme, es bleibt einer frei. |

Im Code geht das: `RmeManager` wird überall über `manager()` angesprochen, die Klasse `Manager`
lässt sich aber ein zweites Mal anlegen (öffentlicher Konstruktor, `RmeManager.h:147`). Der Mixer
bekommt sein eigenes Exemplar mit eigenen Ports und eigenem Zustand. Die Flächen bleiben auf
`manager()`.

## 4. Was aus stoerme übernommen wird

**Unverändert:** die Webseite `public/stoerme.html` und das Logo. Sie verbindet sich auf
`/ws` am selben Host und spricht dieses JSON (gelesen in `bridge.js`):

| Seite schickt | ORC antwortet / tut |
|---|---|
| `hello` mit `pin` | ohne PIN oder mit richtigem: `config` (Version, TotalMix gesehen, Kopfhörerliste mit Sperre); sonst `pin` mit `wrong` und `wait` |
| `select` mit `mix`, `force` | `snapshot` des Kopfhörers, oder `denied`, wenn ein anderer ihn benutzt; mit `force` bekommt der bisherige `released` |
| `set` `fader`/`pan` | schreibt `/mix/<in|pb>/<n>/<bus>/fader` bzw. `balpan`, `update` an alle auf diesem Kopfhörer |
| `mute` | kein Mute pro Routing in Global OSC: Fader auf −∞, alter Wert gemerkt, beim Lösen zurück |
| `solo` | `/mix/<in|pb>/<n>/<bus>/solo` |
| `master` `vol`/`mute` | `/output/<bus>/volume` bzw. `mute`, `master` an alle auf diesem Kopfhörer |

**Ins C++ übertragen** (in ORC neu, nach stoermes Regeln):
- Kopfhörer = `/controlroom/phones1..4`, Namen und Farben aus `/input|playback|output/<n>/…`.
- **Sichtbarkeit pro /sendall-Runde neu**: Nur Kanäle, für die TotalMix in der Runde etwas schickt,
  sind sichtbar; versteckte verschwinden beim nächsten Durchgang. stoerme fragt alle 3 s.
  `RmeManager` fragt heute nur nach dem Verbinden (3 s, 10 s) und auf Anforderung: Das Mixer-Exemplar
  braucht das regelmässige Fragen dazu.
- Nur Änderungen gehen an die Handys, wiederholte Werte aus der Runde bleiben still.
- PIN: SHA-256 und Vergleich in konstanter Zeit (`Sha256.cpp` gibt es schon), 5 Fehlversuche in
  10 min sperren den Absender 10 min, `X-Real-IP` nur von einer privaten Adresse (Reverse Proxy).
  `pin.test.js` wird zum ctest.
- TotalMix-Farbindex auf Hex für die Seite (stoermes Tabelle, dort als „zu kalibrieren“ markiert).

## 5. Neu in ORC

| Baustein | Inhalt |
|---|---|
| **Web-Server** | HTTP für die Seite, WebSocket (RFC 6455, Textrahmen, Ping/Pong, Close) für das JSON. Ein Thread mit `poll()`. ⛔ Kein `select()` (FD_SETSIZE, Memory). Die Seite liegt im App-Bundle unter Resources. |
| **Mixer-Logik** | `orc/Phones.*`: Zustand, Sperre pro Kopfhörer, Mute-Merker, die JSON-Nachrichten. Rein und testbar, ohne Socket. |
| **Zweite Verbindung** | eigenes `Manager`-Exemplar mit Host und Ports aus den Einstellungen, regelmässiges `/sendall`. Läuft auch, während REAPER die Flächen hat. |
| **Einstellungen** | Reiter Phones; gespeichert in ORCs Ordner wie die übrigen. PIN in einer eigenen Datei, nicht in rme.json. |
| **Menü** | Zeile Phones mit Zustand und Adresse, QR-Code. |
| **Handbuch** | Kapitel „Headphone mixes“: TotalMix einrichten (Remote 2, Kanäle verstecken), Adresse, PIN, Zugriff von unterwegs nur hinter einem Reverse Proxy mit PIN. |

## 6. Dein Studio beim Umstieg

stoerme läuft heute auf Remote 2 (7003/7004) und Port 8088, `phones.stoersender.ch` zeigt über die
NAS auf `.83:8088`. ORC nimmt dieselben Werte, dann ändert sich am Proxy und in TotalMix nichts:
LaunchAgent von stoerme stoppen, in ORC Phones einschalten, PIN eintragen. Beide gleichzeitig auf
Remote 2 geht nicht.

## 7. Stufen

| | Inhalt |
|---|---|
| 1 | Mixer-Logik mit Tests: JSON rein und raus, Sperre, Mute-Merker, Sichtbarkeit pro Runde, PIN-Sperre |
| 2 | Web-Server mit WebSocket, ctest für das Rahmenformat |
| 3 | Zweite TotalMix-Verbindung mit regelmässigem Fragen, Übergabe lässt sie offen |
| 4 | Reiter Phones, Menüzeile, QR-Code |
| 5 | Handbuch, dann Umstieg in deinem Studio |

## 7.4 Stand des Baus

| Stufe | Commit | gebaut |
|---|---|---|
| 1 `orc/Phones.*`: stoermes JSON rein/raus, Sperre mit Übernahme, Mute über den Fader mit gemerktem Pegel, Sichtbarkeit pro Runde, PIN-Sperre aus `pin.js`; `Manager::setRounds` (/sendall alle 3 s, 400 ms Fenster, `visible()`); `RmeNames::toUtf8` | `e1a70aa2` | ja, ctest `orc_phones` (Fälle aus `pin.test.js`) |
| 2 `orc/WebSocket.*` (RFC 6455 Server-Seite, HTTP), `orc/PhonesServer.*` (ein Thread, `poll()`), Seite `orc/moreme/moreme.html` + Zeichen `moreme-logo.svg` im Bundle unter Resources/moreme | `30c5b58a` | ja, ctests `orc_websocket` (RFC-Beispielbytes), `orc_phones_server` (echter Server gegen falsches TotalMix, nur Ports vom System) |
| 3 Zweites `rme::Manager` in `PhonesServer`, Host von der Seite General, Ports 7003/7004; Übergabe in `Surface.cpp` stoppt nur `rme::manager()` | `30c5b58a` | ja |
| 4 Reiter **MoreMe**, Menüzeile **MoreMe** mit Adresse, PIN und QR-Code (CoreImage, nur die Adresse) | `30c5b58a` | ja |
| 5 Handbuch-Kapitel „MoreMe: headphone mixes on phones“, Seite MoreMe, Dateien, Fehlerzeilen; `orc_manual.py` prüft die MoreMe-Menütexte | `b1e868d2` | ja, Release offen |

Dateien in ORCs Ordner: `moreme.json` (an/aus, Ports, Studioname, Logo-Dateiname), `moreme.pin`
(0600), das gewählte Logo als `moreme-logo.png|jpg|svg`.

Beim Bauen entschieden oder abgewichen:
- **Reiter und Menüzeile heissen „MoreMe“**, nach 9.1 (der Auftrag nannte sie „Phones“).
- **Ping alle 15 s, ein stummes Handy fliegt nach 45 s** und gibt seinen Kopfhörer frei. stoerme
  hatte das nicht: ein eingeschlafenes Handy hielt seinen Kopfhörer, bis macOS die Verbindung
  aufgab.
- **Neuer PIN schickt alle Handys zur PIN-Eingabe.** stoerme las den PIN nur beim Start.
- **Kein PIN, keine Seite**: der Server öffnet den Port nicht ohne PIN (zweite Sperre neben dem
  Reiter, der nie einen leeren PIN speichert). Beim Start mit „an“ und fehlender PIN-Datei macht
  ORC einen neuen.
- **Nur Änderungen an die Handys**, auch beim Master (stoerme schickte den Master bei jeder Runde).
  Ein in TotalMix umbenannter Kanal schickt einen neuen Snapshot (stoerme nur bei Farbe und
  Sichtbarkeit).
- Ein Phones-Ausgang -1 gilt als nicht zugewiesen (stoerme nahm -1 als Bus).
- Die Seite lädt die Schrift Inter weiter von Google Fonts (1:1); ohne Internet fällt sie auf die
  Systemschrift zurück. Alte `stoerme.*`-Werte im Handy (PIN, Kopfhörer, Ansicht) werden übernommen,
  solange die Adresse gleich bleibt.
- `WsClient.cpp` (OBS) hat seine eigene Rahmen-Logik behalten; `orc/WebSocket` ist nur die
  Server-Seite.
- ORC-Version unverändert 1.3.0 / Build 7. Der Build in /Applications ist ad hoc signiert, die
  notarisierte 1.3.0 liegt im Scratchpad der Session und als DMG in `dist/orc-1.3.0/`.

**Dein Umstieg (Abschnitt 6):** stoermes LaunchAgent stoppen
(`launchctl bootout gui/$(id -u)/ch.stoersender.stoerme`), ORC neu starten, im Reiter MoreMe
einschalten, deinen stoerme-PIN ins Feld PIN. Läuft stoerme noch, zeigt die Menüzeile
„port 8088 is in use by another program“ bzw. „TotalMix port in use by another program“.

## 8. Zu messen (am Gerät, durch dich)

- Meldet TotalMix eine Änderung, die über Remote 2 kommt, sofort an Remote 3? Davon hängt ab, ob
  ein UF8-Fader mitfährt, wenn ein Musiker am Handy schiebt.
- macOS fragt beim ersten Öffnen eines Ports evtl. nach der Firewall-Freigabe. ORC hat keinen
  `NSLocalNetworkUsageDescription`-Eintrag im Info.plist; ob es den für eingehende Verbindungen
  braucht, ist ungeklärt.

## 9. Entschieden (Frank 29.10.) und offen

1. **Name: ORC MoreMe.** Reiter, Menüzeile und Handbuch heissen so; die Seite trägt den Namen
   statt „stoerme“.
2. **Eine Quelle:** Die Seite zieht ins Rea-Sixty-Repo zu ORC, das stoerme-Repo wird nach dem
   Umstieg stillgelegt.
3. **Ab Werk aus:** kein Port, keine zweite Remote, bis man im Reiter Phones das Häkchen setzt.
   TotalMix und die Kopfhörermixe bleiben davon unberührt.
4. **PIN Pflicht, sobald an:** ORC erzeugt beim ersten Einschalten einen vierstelligen PIN, zeigt
   ihn im Reiter und im Menü neben der Adresse, änderbar, nie im QR-Code.
5. **Ein Remote für alle Kopfhörer, wie stoermes Bridge heute**, eine gemeinsame Sichtbarkeit.
6. **Die Seite 1:1** in Aufbau und Verhalten, mit diesen Änderungen:
   - **Logo** wählbar im Reiter Phones (PNG, JPG, SVG), liegt in ORCs Ordner, ab Werk ein
     neutrales MoreMe-Zeichen. **Studioname** als Feld daneben, ab Werk leer (ersetzt das feste
     „Störsender Studio“).
   - „stoerme“/„stoer“ wird **MoreMe**.
   - **Sprache folgt dem Handy:** Deutsch oder Englisch, sonst Englisch (rund 25 Textstellen).
   - „Wir merken uns das auf diesem Gerät“ wird „Dieses Gerät merkt sich die Wahl“ (keine „wir“),
     „Verbinde mit Bridge…“ wird ORC.

## 10. Belege

`~/Documents/dev/stoerme/bridge.js` (Protokoll, Sichtbarkeit, Mute, Farben), `src/pin.js`,
`src/ws.js`, `public/stoerme.html` (Verbindung `/ws`, Nachrichten), `stoerme.config.json`
(Remote 7003/7004, Port 8088), stoermes README (beschreibt noch vier Remotes, die Bridge nutzt
eine), `TotalReaper/README.md` (7001/7002), `src/RmeManager.h:147` und `RmeManager.cpp`
(`/sendall` nur nach dem Verbinden und auf Anforderung), `lsof` am Mac Studio 29.10. (Belegung
7001-7006).
