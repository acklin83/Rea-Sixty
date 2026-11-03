# ORC: Einstellungsfenster als lokale Web-Seite, Plan

Frank 03.11.2026. Plan freigegeben, Mockup abgenommen, Stufen 1 bis 5 gebaut. Stufe 6 (AppKit weg) nach Franks Test.

## Ziel
Das ganze Fenster ORC Settings wird eine Seite im Stil von Studio Control, die ORC selbst ausliefert und in einem eigenen Fenster zeigt. Studio Control zieht in dasselbe Fenster (Frank 03.11.). Dieselbe Seite läuft später unter Windows. Kein Control geht verloren, kein Wert bekommt einen zweiten Besitzer.

Kommt ins selbe Release wie ORC 1.6.0 / Rea-Sixty v0.9.0 (Frank 03.11.: „das neue Settings kommt in dieses Release“). Gebaut auf `main`, das alte Fenster bleibt bis zum letzten Commit unter `Settings (old)…` erreichbar.

## 1. Wer serviert die Seite
Entscheid: ein eigener kleiner Server `orc/SettingsServer.{h,cpp}`, nicht `ph::Server`.

- `ph::Server` läuft nur, wenn MoreMe an ist, und lauscht mit `INADDR_ANY` im Netz. Die Einstellungen müssen immer gehen, auch um MoreMe erst einzuschalten, und gehören nicht ins Netz.
- Bindet nur `127.0.0.1`, Port 0: das System vergibt einen freien Port, jedes Mal neu. Darum kein fester Port und kein Eintrag in einer Datei. Kollision mit MoreMe in die eine Richtung ist unmöglich, weil das System keinen belegten Port vergibt. In die andere Richtung (jemand tippt bei Page port genau diesen Port) lehnt das Modell ab, mit der gleichen Fehlerzeile wie heute.
- Läuft nur, solange das Fenster offen ist. Öffnen startet ihn, Schliessen stoppt ihn. Das ersetzt die heutige Regel „nothing is polled while nobody is looking“ (`windowWillClose`).
- Nutzt `orc/WebSocket.{h,cpp}` unverändert (rein, ohne Socket) und das Muster aus `Server::loop` (poll, nie select, `SO_NOSIGPIPE`). `PhonesServer.cpp` wird dafür nicht umgebaut, nur drei Helfer aus dem anonymen Namespace werden sichtbar: `resourceDir`, die Logo-Antwort, `imageType`.
- Kein PIN. Dafür zwei unsichtbare Riegel, weil über diese Leitung auch das OBS-Passwort und der MoreMe-PIN laufen:
  - Ein Zufallswert pro Öffnen im Pfad (`/s/<token>`, `/ws/<token>`). Nur das ORC-Fenster kennt ihn. Der Nutzer sieht und tippt nichts. Damit kommt kein anderes Programm auf dem Mac und kein anderer Benutzer an die Einstellungen.
  - Host und Origin müssen exakt `127.0.0.1:<port>` sein, mit Port. `deskAllowed` vergleicht ohne Port (siehe „Aufgefallen“), darum eine eigene Funktion.

Alternative verworfen: eigenes URL-Schema plus Script-Message-Brücke, ganz ohne Socket (`WKURLSchemeHandler` am Mac, `WebResourceRequested` unter Windows). Das wären zwei Brücken in zwei Sprachen statt einem Server in C++, und die Tests könnten nicht mehr über einen echten WebSocket fahren.

### Alles in einem Fenster (Frank 03.11.)
Studio Control wird der achte Reiter. Technisch ein `<iframe>` auf `http://127.0.0.1:<webPort>/desk`: `/desk` und `/deskws` bleiben im MoreMe-Server, dessen Thread der einzige Schreiber der Performer bleibt, und `desk.html` bleibt fast wie sie ist. Die Adresse kommt mit dem Zustand vom Modell.

Folgen:
- MoreMe aus oder Seite nicht erreichbar: der Reiter ist gesperrt, wie heute der Knopf Open und der Menüpunkt (`s.enabled && st.listening`).
- `Studio Control…` im Menü öffnet dasselbe Fenster auf diesem Reiter. Die Zeile Studio Control im Reiter MoreMe fällt weg, `OrcDeskWindow.{h,mm}` auch.
- Ein Fenster, eine Grösse: 1280 x 820, Mindestgrösse 620 x 420 wie heute. Kein Grössensprung beim Reiterwechsel. Unter 1000 Punkten Breite stapelt `desk.html` ihre Spalten schon heute.
- ⛔ Der iframe hat einen anderen Port, also einen anderen Origin. Chromium sperrt `confirm`, `prompt` und `alert` aus solchen Frames (seit Chrome 92). Ob WebView2 das übernimmt, dazu habe ich keine Quelle. `desk.html` bekommt darum eigene Dialoge in der Seite statt `confirm` und `prompt`. Im `WKWebView` gingen sie weiter, unter Windows wäre es sonst ein Fehler, den man erst dort sieht.
- Kopieren im iframe: `desk.html` fällt ohne Clipboard-API auf `execCommand` zurück. `allow="clipboard-write"` am iframe kommt dazu (aus dem Gedächtnis, Stufe 5 prüft es in beiden Engines).
- Marke und Seitenname oben in `desk.html` wiederholen nur den Rahmen: `/desk?embed=1` blendet sie aus.

### Darstellung (aus dem Mockup)
- ⛔ `<select>` in WebKit ignoriert Padding und Höhe, die Menüs waren im ersten Bild 24 Punkte hoch neben 38 Punkte hohen Feldern. Alle Controls haben darum eine Höhe (34), `appearance:none` mit eigenem Pfeil. `desk.html` hat dasselbe Problem heute und bekommt in Stufe 5 dieselbe Regel.
- Inter kommt als Dateien ins Bundle (`orc/settings/fonts`, Latin und Latin Ext, vier Schnitte, aus `@fontsource/inter` 5.3.0, OFL 1.1, gelesen in dessen LICENSE, Text in `THIRD-PARTY-NOTICES.txt`), statt von Google: die Seite muss ohne Netz gleich aussehen. Inter Tight lag nicht vor, die Marke ist Inter 900 kursiv.
- Soft Keys: die zehn Bänke als Liste links statt Menü, wie die Performer in Studio Control. Gleiche Funktion, aber man sieht, was auf jeder Bank liegt.

## 2. Lesen und Schreiben

### Eine Kopie der Logik
Neu `orc/OrcSettingsModel.{h,cpp}`, reines C++ ohne Socket und ohne AppKit, im Stil von `Performers.cpp` (`applyDesk`):
- `stateJson()`: liest die Manager und baut den Zustand für die Seite.
- `apply(json)`: eine Nachricht von der Seite, ein Aufruf derselben Setter wie heute.

Dorthin wandert alles, was heute in den `.mm`-Dateien Logik ist: `targetChoices`, `outputChoices`, `rmeActions`, `actionTitle`, `kTransport`, `kPushes`, die Regeln aus `skLabelChanged`, `skActionChanged`, `skKindChanged`, `speedChanged`, `sleepChanged`, `validPin`, `obsChanged`, `hueTargetChanged`. Abweichung vom ersten Plan: weil alles in einem Zug ins selbe Release geht, bleiben die AppKit-Reiter unangetastet und das Modell hat bis zu Stufe 6 eine Kopie dieser Regeln. Die Tests prüfen das Modell gegen die Regeln aus dem AppKit-Code, Stufe 6 löscht die alte Kopie. Der Umbau bleibt so bis zuletzt einfach rückgängig zu machen.

Die Seite rechnet nichts. Sie bekommt Listen und je Feld, was gewählt ist, und schickt, was der Nutzer geändert hat.

### Wer schreibt was (gegreppt, Stand heute)
| Datei | Besitzer im Code | Schreiber heute | Nach dem Umbau | Leser |
|---|---|---|---|---|
| `rme.json` | `rme::manager()`, `orc::saveRmeConfigIfDirty` | `main.cpp` (erster Start), Fenster, Surface-Thread (`stepLight` in `Surface.cpp`, gespeichert im Loop) | Settings-Thread statt Fenster, Rest gleich | Surfaces, `PhonesServer` (Host), Rea-Sixty |
| `orc.json` | `uf8::bindings` (`g_cfgMutex`, `persistLocked_`) | Fenster über die Setter | Settings-Thread über dieselben Setter | Surfaces, Rea-Sixty (`refreshSideCarSource`) |
| `modes/*` | `orc::modes` | Fenster direkt nur `dynamount_devices`, `dynamount_fill`. Der Rest über Dirty-Flags in `saveIfDirty` auf dem Surface-Thread. Dazu `importFromReaper` (Fenster) und `syncIn` (Übergabe) | Settings-Thread statt Fenster, Rest gleich | Manager, Rea-Sixty über die Übergabe |
| `moreme.json`, `moreme.pin` | `ph::saveSettings`, `ph::savePin`, `Server::want_` | Reiter MoreMe, `main.cpp` (PIN beim Start) | Settings-Thread statt Reiter | `Server` |
| `moreme-performers.json` | MoreMe-Thread | nur der | unverändert, nicht angefasst | `Server` |

### Welcher Thread
Entscheid: der Settings-Thread übernimmt genau die Rolle, die heute der Main-Thread als Schreiber hat. Keine Weiterleitung auf den Main-Thread.

- Gelesen: `Manager::setConfig`/`config` (`mx_`), `getBinding`/`setBinding`/`getUf1SoftBankSlot` (`g_cfgMutex`), Hue (`cfgMx_`), DynaMount (`cfgMx_`, `presetMx_`), OBS (`mx_`). Der Surface-Thread greift heute schon parallel darauf zu. In Stufe 4 gehe ich jeden Hue-, OBS- und DynaMount-Setter einzeln durch, die habe ich bisher nur an den Stichproben gelesen.
- Main-Thread-Pflicht hat nur, was AppKit ist. Das bleibt beim Fenster (Abschnitt 3).
- `Server::apply` ist mit „Main thread“ kommentiert, weil `worker_` keinen Schutz hat. Das gilt weiter sinngemäss: nach dem Start gibt es genau einen Aufrufer, den Settings-Thread. Der Kommentar wird angepasst. `main.cpp` stoppt den Settings-Server vor `surface.stop()` und `ph::server().stop()`.
- Vorteil gegenüber Main-Thread: ein Stopp von MoreMe (`join`) blockiert nicht die Menüleiste und Dialoge blockieren keine Änderung.
- Was gleich bleibt: Lesen, ändern, zurückschreiben eines ganzen `Config` kann heute schon mit `stepLight` kollidieren (Main gegen Surface). Der Umbau ändert daran nichts, er tauscht nur den einen Thread.

### Zustand zur Seite
Wie `/deskws`: ganzer Zustand als JSON über den WebSocket, gesendet, wenn sich der Text geändert hat. Einmal pro Sekunde verglichen wie `refresh` heute, nach jeder Änderung sofort. Bank und Hälfte von Soft Keys sind Sache der Seite, der Zustand enthält alle zehn Bänke.

### Was aus dem AppKit-Code mitmuss, sonst geht es still kaputt
- Ein Ziel, das die Liste gerade nicht enthält (Link weg, Kanal versteckt): keine Auswahl anzeigen und die Datei nicht anfassen (`select:spec:among:` mit `rmeu::menuSpec`). ⛔ Ein `<select>` fällt von selbst auf den ersten Eintrag zurück. Die Seite setzt `selectedIndex = -1` ausdrücklich, ein Test prüft es.
- Keine Liste unter der Hand neu bauen: kein Neuzeichnen, solange ein Feld den Fokus hat, auch bei `<select>`. `desk.html` schützt heute nur Textfelder.
- Soft Keys und Transport haben verschiedene Regeln für fremde Aktionen: Soft Keys zeigen keine Auswahl, Tasten zeigen None.
- `value` statt Position: doppelte Hue-Szenen verlieren die Falle aus `addEntry`.
- Textfelder übernehmen bei Enter und beim Verlassen, wie heute (`change`, nicht `input`). Beim Schliessen des Fensters wird das offene Feld noch übernommen.
- Grenzen bleiben im Modell: Speed 0.10 bis 2.00, Sleep 1 bis 99, Ports 1 bis 65535, PIN 4 bis 8 Ziffern (bei Fehler zurück auf den alten Wert plus Fehlerzeile), Hue-Label 8 Zeichen, Szenen-Label 12.
- OBS-Passwort geht nur von der Seite zu ORC, nie zurück. Das Feld zeigt Punkte, solange eines gesetzt ist. Leeren und verlassen löscht es.
- Farbmenüs mit Musterfeld: ein `<select>` kann das nicht. Eine kleine eigene Liste, Name plus Farbfeld, nur was leuchtet (`paletteSwatch`). Bei den DynaMount-Ständern nur das Farbfeld.

## 3. Was Mac-spezifisch bleibt
`struct SettingsHost` mit Funktionszeigern, wie `uf8::bindings::Host`: das Modell kennt kein AppKit, das Fenster setzt die Antworten ein. Die Seite schickt eine normale Nachricht, der Settings-Thread ruft den Host, der Host geht per `dispatch_async` auf den Main-Thread.

| Was | Am Mac | Weg von der Seite |
|---|---|---|
| Open at login | `SMAppService`, `runningFromApplications`, `openSystemSettingsLoginItems` | Nachricht `login`. Der Zustand kommt andersherum: der 1-Sekunden-Timer des Fensters reicht ihn an den Server, nie ein `dispatch_sync` aus dem Settings-Thread |
| Logo wählen | `NSOpenPanel`, dann `ph::importLogo` unverändert | Nachricht `chooseLogo`. Der Pfad kommt zurück in die Warteschlange des Settings-Threads. Kein Upload, kein HTTP-Body im Server |
| Menüleiste, QR im Menü, Manual, Quit | `OrcApp.mm` | bleibt, keine Seite. `Settings…` und die 360-Taste rufen weiter `present` und `toggle` |
| Fenster | neu `orc/OrcWebWindow.{h,mm}`: `WKWebView`, Dialoge und `window.open` aus `OrcDeskDelegate` dorthin verschoben | ATS steht schon in `Info.plist.in` |
| Cmd-C, V, X, A in Feldern | ORC hat kein Hauptmenü (`mainMenu` kommt in `orc/*.mm` nicht vor), ohne Edit-Menü tun die Tasten im `WKWebView` nichts (Glimpse, siehe Quellen). Unsichtbares Edit-Menü, Accessory-Apps zeigen keine Menüleiste | gilt für den ganzen Fenster-Inhalt |

Fenster: Titel ORC Settings. Reisst der Web-Prozess ab, lädt das Fenster neu, die Seite verbindet sich selbst wieder (Muster `desk.html`).

## 4. Windows
Die Milestone ORC für Windows hat eigene Pläne für USB, Tray, Übergabe. Hier nur, was das Einstellungsfenster betrifft.

Läuft unverändert: `settings.html`, `OrcSettingsModel`, `WebSocket.cpp`, die Manager und Bindings, die Rea-Sixty unter Windows schon baut, und das Verhalten von `SettingsServer`.

Kommt dazu:
- Fenster mit WebView2 statt `OrcWebWindow.mm`. Dialoge von `confirm` und `prompt` liefert WebView2 selbst.
- Laufzeit: Evergreen. Unter Windows 11 Teil des Systems, unter Windows 10 auf den meisten Rechnern da, aber nicht auf allen, darum prüft der Installer und zieht bei Bedarf den Bootstrapper von Microsoft (Quellen). Kein Fixed Version: wir müssten die Laufzeit selbst mitliefern und aktuell halten.
- Daten der Web-Ansicht unter `%LOCALAPPDATA%`, nicht neben dem Programm (aus dem Gedächtnis, vor dem Bau gegen die Doku prüfen).
- Socket-Teil in `SettingsServer`: `WSAPoll`, `closesocket`, Zufall aus `BCryptGenRandom`. Kein `SO_REUSEADDR` (Memory `windows-so-reuseaddr-hijacks-port`), bei Port 0 auch nicht nötig. Kein `SO_NOSIGPIPE`.
- `SettingsHost` unter Windows: `IFileOpenDialog` für das Logo, Registry `HKCU\...\Run` für Open at login.
- Pfade in `OrcConfig.cpp` (`supportDir`), `resourceDir`, Import aus REAPER (`importFromReaper`): Teil der Milestone.
- Die Firewall fragt bei reinem Loopback nach allem, was ich fand, nicht nach. Ich habe dazu aber keine Microsoft-Quelle gefunden, nur allgemeine Quellen. Wird am StoerPC gemessen, bevor es im Plan der Milestone als Tatsache steht.

Die Engine dort ist Chromium, am Mac WebKit. Ab Stufe 1 wird die Seite darum in beiden geprüft: WebKit offscreen mit eigenem Programm, Chromium im Browser-Pane.

## 5. Stufen
Das AppKit-Fenster bleibt vollständig und läuft daneben, unter `Settings (old)…` im Menü. Beide schreiben über dieselben Funktionen und zeigen sich gegenseitig innert einer Sekunde an. Studio Control behält sein eigenes Fenster, bis Stufe 5 den Reiter bringt. Jede Stufe baut, läuft und hat ihre Tests. Der letzte Schritt ist ein einziger Commit, der AppKit entfernt. Released wird erst danach, im selben Release wie die Submix-Tasten und MoreMe Performer.

Regel für alle Tests und die Demo: HOME auf einem Scratch-Ordner, nur Ports vom System, `rme::manager()` nie gestartet, keine Worker für Hue, OBS, DynaMount. ⛔ MoreMe einschalten startet einen echten TotalMix-Remote: Test und Demo schreiben vorher `moreme.json` mit Ports vom System, die auf ein falsches TotalMix zeigen (`FakeMix` aus `tests/test_orc_phones_server.cpp`), nie 7003/7004. Go, Home, Detect werden nur ohne Worker und nur auf die Anfrage hin geprüft.

**Stufe 0, Mockup (gebaut 03.11.).** Alle acht Reiter in den Farben und Schriften von `moreme.html`, Navigation oben und links umschaltbar, Studio Control ist die echte `desk.html` mit Beispieldaten. Geprüft offscreen in WKWebView. Liegt im Scratchpad, nicht im Repo, speichert nichts.

**Stufe 1, Gerüst und General.** `SettingsServer`, `OrcSettingsModel`, `OrcWebWindow`, `SettingsHost` mit Open at login, `orc/settings/settings.html`, CMake (Quellen, Kopie ins Bundle vor der Signatur, wie `moreme/`).
Test `test_orc_settings_server`: nur Loopback gebunden, falscher Token, falscher Host, fremder Origin, Origin mit anderem Port, `X-Forwarded-For` werden abgewiesen. Seite kommt, WebSocket liefert den Zustand, Host-Änderung landet in `rme.json`, unveränderter Wert schreibt nichts.

**Stufe 2, UF1, UF8, Colours.** Ziellisten, Output-Listen, Farbliste, Tasten-Tabellen. `targetChoices` usw. wandern ins Modell, AppKit ruft sie von dort.
Test `test_orc_settings_model`: Ziel nicht in der Liste bleibt unverändert in der Datei, Rolle über `menuSpec`, Grenzen, `orc.json` trägt Aktion und Lampenfarben, fremde Aktion bei Transport zeigt None. WebKit-Prüfung: `selectedIndex -1`, keine Neuzeichnung unter offenem Menü.

**Stufe 3, Soft Keys.** Bank, Hälfte, Shows, Name, Label, Aktion, Lampen, Factory.
Test: Label Hälfte 0 auf `Binding::label` mit `labelIsUserSet`, Hälfte 1 auf `ActionSlot::label`, Lampe bleibt bei Aktionswechsel, Wechsel von Submix weg leert die Specs, Kind gilt für beide Hälften, Factory stellt nur diese Bank zurück. Gegenprobe gegen den heutigen AppKit-Pfad: gleiche Eingaben, gleiche `orc.json`.

**Stufe 4, UF8 Modes.** Import, OBS, DynaMount, Hue.
Test ohne Worker: `modes/`-Dateien und Stempel, OBS-Passwort steht nie im Zustand, doppelte Szenennamen wählen die richtige Szene, Hue-Label auf 8 Zeichen gekürzt.

**Stufe 5, MoreMe und Studio Control.** Ein/aus, PIN, Ports, Studioname, Logo, Namen, Adresse, Status. Reiter Studio Control mit iframe, `?embed=1`, eigene Dialoge und Select-Regel in `desk.html`.
Test: erstes Einschalten erzeugt PIN, ungültiger PIN bleibt beim alten, Page port gleich dem Settings-Port abgelehnt, Logo über `importLogo` und Vorschau über den Settings-Server, `apply` auf dem Settings-Thread startet und stoppt mit `FakeMix`. `/deskws` nimmt den iframe an, Reiter gesperrt bei MoreMe aus. Dialoge und Kopieren im iframe in WebKit und Chromium.

**Stufe 6, Umstieg.** `Settings…`, `Studio Control…` und 360 öffnen die Seite. Weg: `OrcSettingsWindow.mm` bis auf den Fensteraufruf, `OrcModesTab`, `OrcPhonesTab`, `OrcLayout.h`, `OrcDeskWindow`, Zusatzpunkt im Menü, AppKit-Einträge in CMake. Prüfung der Vollständigkeit als Test: die Liste der Controls aus dem Bestand unten gegen die `data-id` in `settings.html`.
Handbuch: Abschnitt The settings window und die Anker `set-*` bleiben, Reiter und Namen auch. `tools/orc_manual.py --check` liest die Status-Texte aus `OrcConfig.cpp`, `OrcApp.mm`, `Surface.cpp`, `PhonesServer.cpp`, nicht aus dem Fenster, und bleibt darum gültig. Dazu `COMPENDIUM.md`, Kommentare in `main.cpp` und `OrcUi.h` („AppKit, not ImGui“ gilt weiter für Menüleiste und Rahmen).

## Bestand: jedes Control
Reiter, Namen und Reihenfolge bleiben, das Handbuch verweist darauf. Neu dazu kommt der Reiter Studio Control.

| Reiter | Controls | Schreibt über |
|---|---|---|
| General | Talk to TotalMix, Host, Send to port, Listen on port, Sleep when idle, After | `setConfig` |
| | Open at login | Host |
| | Status, Ask TotalMix again, Fehlerzeile | `linkSummary`, `manager().refresh()` |
| UF1 | Use the UF1; 8 x Pot (Bank 1, 2): Target, Push; Jog wheel, Jog step, Pot step, V-Pot speed | `setConfig` |
| | 5 x Transport: Action, Active, Inactive | `setBinding` |
| UF8 | Use the UF8, Show Main volume on change, V-Pot speed, 8 x SEND/PLUG-IN Output | `setConfig` |
| | 9 x Keys (QUICK 1-3, NAV, NUDGE, FOCUS, NORM, REC, AUTO): Action, Active, Inactive | `setBinding` |
| Soft Keys | Bank (10), Half, Shows (5 Arten), Name; 4 x Key-Nummer, Label, Action (bei Submix: Output), Active, Inactive; Factory set for this bank | `setUf1SoftBank*`, `setSubmixSpec`, `restoreRmeSideCarBank` |
| Colours | 9 x TotalMix-Name, UF1-Farbe, Channels (nur Anzeige) | `setConfig` |
| UF8 Modes | Import from REAPER mit Ergebnis | `importFromReaper` |
| | OBS: Talk to OBS, Host, Port, Password, Status | `ObsManager` |
| | DynaMount: Fill; 8 x On, Name, IP, Colour, Detect, Home, Status; Presets 8 x Name, Stands, Go, Store, Clear | `DynaMountManager`, `modes::write` |
| | Hue: Find bridge, Pair, Cancel, Refresh, Forget, IP, Status, Fill; 8 x On, Label, Target; Scenes 8 x Scene, Key label; V-Pot, V-Pot + FLIP | `HueManager` |
| MoreMe | Headphone mixes in the browser, Address (markierbar), PIN, Page port, TotalMix send, TotalMix listen, Status | `saveSettings`, `savePin`, `apply` |
| | Studio name, Logo-Vorschau, Choose, Default Logo, 4 x Phones-Name, Fehlerzeile | dito, `importLogo` |

Dazu: Tooltip „Only for the ORC in Applications“ am gesperrten Open at login, Platzhalter der Felder (Labels aus `softKeyFallbackLabel`, Preset N, mic name, 192.168.1.50, Phones N).

## Was ich nicht baue
Keine neuen Einstellungen, keine Hilfetexte auf den Seiten (Frank 27.09.), keine Suche, keine Bedienung vom Tablet, keinen PIN auf der Seite. Keine Änderung an den Dateiformaten, Rea-Sixty liest `rme.json` und `orc.json` weiter wie bisher. Kein Umbau von `PhonesServer.cpp` über die drei Helfer hinaus. Keine Teile der Windows-Milestone ausser dem, was hier steht.

## Aufgefallen, nicht Teil des Plans
- `saveRmeConfig` und `orc_handover::writeFile` schreiben direkt in die Datei, ohne temporäre Datei und Umbenennen, und `takeConfigDirty` schützt nicht vor zwei gleichzeitigen Schreibern (Fenster, Surface-Thread). Rea-Sixty liest `rme.json`. Was der Leser bei einer halben Datei tut, habe ich nicht untersucht.

## Entschieden (Frank 03.11.)
- Nur dunkel wie Studio Control.
- Alles in einem Fenster, Studio Control als Reiter.
- Unsichtbarer Zufallswert in der Adresse, kein PIN.
- Leiste links, Off grau, auch in Studio Control.
- `deskAllowed` vor dem Release gefixt (`44c038f6`).
- Das neue Settings kommt ins selbe Release.

## Noch offen
- `rme.json` nicht atomar geschrieben (siehe „Aufgefallen“): Frank hat ihn nicht freigegeben.

## Belegt und nicht belegt
Aus dem Code gelesen: alle Tabellen oben, Sperren und Schreiber. Gelaufen: `test_orc_settings`, die Seite gegen den echten Server offscreen in WebKit (Scratch-HOME, falsches TotalMix). Nicht gelaufen: das Fenster selbst (`OrcWebWindow.mm`), Open at login, der Logo-Dialog, Cmd-C und Cmd-V, Chromium. Hue-Setter nur an den Sperren geprüft: `hm.setSlot` und Co. ohne Bridge, Auswahl aus echten Lampen und Szenen nicht.
Aus dem Netz: WebView2-Verteilung, Edit-Menü. Keine Microsoft-Quelle: Firewall bei Loopback. Aus dem Gedächtnis: Ablage der WebView2-Daten, `WSAPoll`, `IFileOpenDialog`, Registry-Pfad, `SMAppService` nur vom Main-Thread aufrufen (darum `dispatch_async`, ohne Beleg, dass es nötig wäre).

## Quellen
- WebView2-Verteilung: [Microsoft Learn, Distribute your app and the WebView2 Runtime](https://learn.microsoft.com/microsoft-edge/webview2/concepts/distribution), [Evergreen vs. Fixed Version](https://learn.microsoft.com/en-gb/microsoft-edge/webview2/concepts/evergreen-vs-fixed-version)
- Edit-Menü im WKWebView: [Glimpse, Release Notes](https://github.com/hazat/glimpse/releases), [BetterDisplay #1957](https://github.com/waydabber/BetterDisplay/issues/1957). Keine Quelle speziell zu Accessory-Apps, darum gehört das in Stufe 1 auf den Prüfstand.
- Dialoge aus fremden Frames: [Chrome 92, Entfernung der Dialoge aus Cross-Origin-Iframes](https://frontendfoc.us/link/112586/rss), [WebView2 ScriptDialogOpening](https://learn.microsoft.com/en-us/dotnet/api/microsoft.web.webview2.core.corewebview2.scriptdialogopening)
- Loopback und Firewall, nur allgemein: [Gridinsoft](https://blog.gridinsoft.com/what-is-127-0-0-1/), [Pinggy](https://pinggy.io/amp/blog/what_is_127_0_0_1_and_loopback/)
