# UF1: eigene Jog-Modi

Stand 16.11.2026, Plan, nichts gebaut. Frank: „einen weg für user-defineable jog-wheel modes“.
Ein früherer Plan dazu ist weder im Repo noch in der Memory auf dem Mac Studio zu finden.

## Was es tut

Neben Playhead, Scrub, Items, Envelope, Razor, Fades und Lanes gibt es Plätze für eigene Jog-Modi.
Ein eigener Modus hat einen Namen, steht im Karussell (SCRUB halten + Jog) wie die eingebauten,
hat sein eigenes Nav-Kreuz, und das Rad tut, was der Nutzer festlegt: pro Raste eine Aktion je
Richtung, oder eine REAPER-Action, die die Drehung mit Vorzeichen bekommt.

## Wie es heute gebaut ist (gelesen)

- **Modi fest im Code:** `enum class Uf1JogMode` (main.cpp:7033), sieben Werte. Das Rad läuft
  durch `uf1JogDispatch_` (main.cpp:15819), ein `switch` pro Modus. Picker: `uf1JogModeStep_`,
  aufgerufen in main.cpp:23557, solange SCRUB gehalten ist.
- **Das Rad ist keine Bindung:** `ButtonId::Uf1Jog` zeigt im Editor nur die Jog-Einstellungen,
  keine Aktionsauswahl (SettingsScreen.cpp:5010).
- **Encoder-Bindungen kennen keine Richtung für REAPER-Actions:** `dispatchEncoder`
  (Bindings.cpp:6768) feuert REAPER-Action, Taste und MIDI einmal pro Raste, egal in welche
  Richtung (Bindings.cpp:6786-6791). Nur Built-ins bekommen die Raste mit Vorzeichen als `param`.
- **REAPER-Actions laufen über** `runStep_` und das Built-in `__reaper_action__` (Bindings.cpp:5568),
  also `Main_OnCommand`-artig, ohne Wert.
- **Kreuz pro Jog-Modus:** fünf ButtonId-Blöcke `Uf1NavUpPlayhead…Uf1NavDownLanes`
  (Bindings.h:235-244), je 7, `kUf1JogModeCountForNav = 7` (Bindings.h:332), main.cpp:7040
  prüft per `static_assert`, dass enum und Block gleich gross sind. Gespeichert per Namen, neue
  Modi werden pro Block hinten angehängt (Kommentar Bindings.h:228-234). Leere Plätze füllt
  `fillDerivedUf1Slots_` aus der physischen Taste (Bindings.cpp:947).
- **Pro Modus:** Einheit, Schritt, Fader folgt (`g_uf1JogUnit/Step/Fader[kUf1JogModeCount]`,
  main.cpp:7277-7283), Ring für Reihenfolge und Sichtbarkeit `encring::Ring<kUf1JogModeCount>`
  (main.cpp:7289), ExtState `uf1_jog_seq` und `uf1JogVis<m>`. `kUf1JogModeCount` steht 29-mal
  in main.cpp.
- **Namen:** `uf1JogModeFriendly` (main.cpp:7334) und `uf1JogModeHdr_` (main.cpp:7347), beide ein
  `switch` mit festen Texten. Sie gehen auch an den Modus-Banner und das Karussell im Companion
  (`uf1_jogmodes`, main.cpp:47859).
- **Auswahl per Aktion:** Built-ins `jog_mode_*` (Bindings.cpp:8088ff, Doku 9482ff) und
  REAPER-Actions `g_cmdJogMode*` (main.cpp:61631ff).
- **REAPER kann relative Eingabe:** `KBD_OnMainActionEx(cmd, val, valhw, relmode, hwnd, proj)`
  (reaper_plugin_functions.h:4084, „relmode absolute (0) or 1/2/3 for relative adjust modes“).
  REAPERs eigene OSC-Fläche schickt eine relative Drehung mit relmode 1 und
  `encode_relmode1_extended` (reaper_csurf/csurf_osc.cpp:561 und 2750). Das ist der Weg, auf dem
  REAPER-Actions mit „(MIDI CC relative/mousewheel)“ im Namen die Menge und Richtung bekommen.

## Bausteine

### A · Feste Zahl Plätze, hinten an das enum

`Uf1JogMode` bekommt `User1 … User4` hinten angehängt (Zahl = Frage 1). Kein dynamisches
Anlegen: Arrays, Ring und Kreuz-Blöcke sind feste Grössen, eine Liste beliebiger Länge hiesse
alle 29 Stellen umbauen. Ein Platz ohne Aktion ist im Karussell aus (Sichtbarkeit ab Werk aus),
bis er eine Aktion hat.

Mitziehen: `kUf1JogModeCountForNav` auf 11, die fünf Kreuz-Blöcke je um `…User1…User4`
verlängern (angehängt, Namen `uf1_nav_up_user1` usw.), `fillDerivedUf1Slots_` füllt sie wie die
anderen. Beide `switch` für Namen, `uf1JogDispatch_`, die Werks-Einheiten (main.cpp:7329).

### B · Was das Rad tut

Pro Platz zwei neue ButtonIds, `Uf1JogUser1Left` und `Uf1JogUser1Right`, ganz normale Bindungen
mit vier Modifier-Plätzen. Der Editor ist der vorhandene: Aktion, Kette, Built-in, Taste, MIDI.
Zwei Arten pro Platz (Frage 2):

1. **Links / Rechts:** jede Raste nach links feuert die Links-Bindung, nach rechts die
   Rechts-Bindung. Für alles, was es nur als „einen Schritt“ gibt (Marker vor/zurück, nächste
   Spur, Take wechseln, eigene Skripte).
2. **Eine Aktion mit Richtung:** nur die Rechts-Bindung zählt, sie bekommt die Rasten mit
   Vorzeichen. REAPER-Action: neuer Weg über `KBD_OnMainActionEx` mit relmode 1, ein neuer
   Host-Haken neben `__reaper_action__`, weil `Bindings.cpp` auch in ORC läuft und REAPER dort
   nicht kennt. Built-in: wie heute bei `dispatchEncoder`, die Raste als `param`.

**Teiler:** Rasten pro Auslösung (1 = jede Raste), pro Platz. Der Jog liefert viel mehr Rasten
als ein Encoder; ohne Teiler springt „nächster Marker“ beim leichten Drehen fünf Marker weit.

**SHIFT** (Frage 3): eigener Modifier-Platz wie bei jeder Bindung, leer = Plain mit feinerem
Teiler (Teiler mal „Shift fine div“, das es für den Jog schon gibt). Cmd und Ctrl: eigene Plätze,
leer = Plain (die Regel „leerer Modifier-Platz feuert Plain“ gilt schon für globale Tasten).

### C · Name

Pro Platz ein Name, höchstens 8 Zeichen (UF1-Textbreite), ab Werk „User 1“ bis „User 4“.
Steht im Karussell, im Modus-Banner, im Settings-Fenster und in der Kopfzeile, wo heute
`uf1JogModeHdr_` steht. Das Zeitfeld zeigt in eigenen Modi die REAPER-Zeit wie Playhead.

### D · Auswahl

Built-ins `jog_mode_user1 … jog_mode_user4` und REAPER-Actions „UF1 Jog Mode: User 1“ usw., wie
die sieben vorhandenen. Je Beschreibung, Kurzlabel, Kategorie (CI prüft das mit
`check_builtin_docs.py`). `jog_mode_cycle` und der Picker laufen über den Ring, nehmen sie also
von selbst mit, sobald sie sichtbar sind.

### E · Speichern

- Aktionen: bindings.json über die neuen ButtonIds (pro Ebene, wie jede Bindung).
- Name, Art, Teiler: auch bindings.json, damit Export und Mapping Exchange sie mitnehmen. Neues
  Feld in `Config`, Bindings-Version hoch, ältere Dateien laden ohne Eintrag = leere Plätze.
- Reihenfolge und Sichtbarkeit: ExtState wie bei den anderen Modi (`uf1_jog_seq`, `uf1JogVis<m>`).

### F · Settings-Fenster

Im Jog-Abschnitt (SettingsScreen.cpp:5010) stehen die vier Plätze in der Liste wie die anderen
Modi. Ist ein eigener Modus aktiv, zeigt der Abschnitt darunter Name, Art, Teiler und zwei
Knöpfe, die die Links- und Rechts-Bindung im normalen Bindungseditor öffnen. Kein eigener
Aktionseditor.

### G · Companion, Handbuch, Tests

- Companion-Lua: Banner und Karussell lesen die Namen aus dem C-String, sie kommen ohne
  Lua-Änderung an. Prüfen, ob die Lua irgendwo die sieben Modi selbst aufzählt; sie steckt in der
  dylib.
- Handbuch: Abschnitt „Your own jog modes“ unter Jog, dazu die neuen Built-ins in der Liste.
- Tests: `perModeNavId` und `splitPerModeNavId` für Modus 7 bis 10, Name und Felder hin und
  zurück durch bindings.json, Teiler (5 Rasten bei Teiler 3 = 1 Auslösung, Rest bleibt),
  relative Kodierung (+1, -1, +7, -70 klemmt bei -64) gegen REAPERs `encode_relmode1_extended`.

## Wer fasst diese Werte sonst noch an

- `kUf1JogModeCount`: 29 Stellen in main.cpp. Jede wird gelesen, nicht gezählt; jedes Array
  wächst mit, jede `switch` bekommt die neuen Fälle (ein `default` verdeckt sonst Fehler).
- `Uf1JogMode::Lanes` als „letzter Modus“: `kUf1JogModeCount = Lanes + 1` (main.cpp:7035) muss
  auf `User4 + 1`.
- Die Abfragen `liveJm == 2/3/4/5/6` in SettingsScreen.cpp zählen auf Zahlen. Neue Werte ab 7
  stören sie nicht, solange angehängt wird.
- ORC: eigene Jog-Ziele (TotalMix, Systemlautstärke), teilt `Bindings.cpp`. Die neuen ButtonIds
  sind UF1-Rea-Sixty-Ids und müssen aus ORCs Editor draussen bleiben; der neue Host-Haken ist
  in ORC leer.
- Rea-Sixtys Side-Car (ORC-Bänke auf dem UF1 bei laufendem REAPER) liest die Jog-Modi nicht.

## Fallen

- **Undo:** viele REAPER-Actions legen pro Aufruf einen Undo-Schritt an. Ein Dreh am Jog sind
  schnell 50 Schritte. Nur im Handbuch nennen, nicht abfangen (REAPER entscheidet das pro Action).
- **Ketten mit Wartezeit** auf einer Jog-Bindung stauen sich beim schnellen Drehen in der
  Warteschlange. Vorschlag: der Editor sperrt Wartezeiten auf diesen Ids.
- **Relative Eingabe gibt es nur für REAPER-Actions des Hauptbereichs**, nicht für den
  MIDI-Editor. Der Picker zeigt ohnehin nur den Hauptbereich (prüfen).
- **Welche REAPER-Actions relativ arbeiten**, erkennt man nur am Namen; Rea-Sixty prüft das
  nicht. Eine nicht relative Action bekommt bei Art 2 einen Wert und tut, was sie bei einem
  MIDI-Wert tut.

## Nicht in diesem Plan

Eigene Encoder-Modi für den UF8 oder UC1 (Frage 4), eigene Anzeige pro Modus, Fader folgt dem
eigenen Modus, Werksbeispiele.

## Geprüft und nicht geprüft

Gelesen in dieser Sitzung: alle Stellen mit Datei:Zeile oben, die SDK-Stellen zu
`KBD_OnMainActionEx` und relmode. Nicht geprüft: ob die Companion-Lua die Jog-Modi selbst
aufzählt, wie der REAPER-Action-Picker Bereiche filtert, welche REAPER-Actions „(MIDI CC
relative/mousewheel)“ heissen (nur aus REAPERs Aktionsliste zu sehen), ob relmode 1 bei allen
relativen Actions dasselbe Tempo ergibt.

## Fragen an Frank

1. Wie viele Plätze? Vorschlag 4.
2. Beide Arten anbieten (Links/Rechts und eine Aktion mit Richtung), oder nur eine?
3. SHIFT: eigener Platz mit Rückfall auf „Plain, feiner“, oder immer nur „feiner“?
4. Nur UF1, oder sollen dieselben Plätze auch als Encoder-Modi auf UF8 und UC1 wählbar sein?
