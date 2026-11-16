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

- Companion: die Lua zählt die Modi nicht selbst auf, `drawJog` (rea_sixty_focused_panel_imgui.lua:542)
  zeichnet die Liste aus ExtState `uf1_jogmodes`. **Aber** diese Liste baut main.cpp:47837-47862
  aus einer eigenen Tabelle Modus zu Built-in (`kJog`), und ein Modus ohne Eintrag fällt still
  heraus (`if (!*b) continue;`). Die vier `jog_mode_user*` müssen in diese Tabelle, sonst fehlen
  sie im Panel-Dropdown, während Karussell und Settings sie zeigen. Besser: die Tabelle neben
  `uf1JogModeFriendly` an einen Ort ziehen, den Builtin-Registrierung und Panel beide lesen.
- Handbuch: Abschnitt „Your own jog modes“ unter Jog, dazu die neuen Built-ins in der Liste.
- Tests: `perModeNavId` und `splitPerModeNavId` für Modus 7 bis 10, Name und Felder hin und
  zurück durch bindings.json, Teiler (5 Rasten bei Teiler 3 = 1 Auslösung, Rest bleibt),
  relative Kodierung (+1, -1, +7, -70 klemmt bei -64) gegen REAPERs `encode_relmode1_extended`.

### H · Der Fader im eigenen Modus

Frank 16.11.: „was ist mit fader integration? für item jog-mode haben wir das ja z.B. auch drin“.
Grundlage ist Franks Satz vom 25.09., der im Code steht (main.cpp:7279): Jog-Modi sollen „auch
den Fader mitnehmen können wenn gewünscht (pro Mode)“.

#### Wie „Fader = Item Volume“ heute gebaut ist (gelesen)

- **Schalter:** `g_uf1JogFader[kUf1JogModeCount]` (main.cpp:7283), ExtState `uf1JogFader<m>`,
  ab Werk aus, geladen main.cpp:61460. Angeboten nur, wo `reasixty_uf1JogModeHasFader` ja sagt,
  heute nur Items (main.cpp:55233). Im Fenster ein Haken „Fader = Item Volume“
  (SettingsScreen.cpp:5035).
- **Drei Ketten im UF1-Kanalmaler** (`uf1PaintChannel_`), das Item-Ziel steht in allen dreien
  vorne, vor Strip Mode, Send, Sticky Pot, FLIP und Extender (main.cpp:39280, Kommentar „First in
  all three chains“):
  1. **Anzeige** (main.cpp:39288-39328): `Uf1FaderDb` mit Wert, Einheit, Name
     (main.cpp:35382). Ein gesetzter Name ersetzt den Spurnamen in der Zone (main.cpp:35494),
     ein gesetzter Wert den dB-Wert (main.cpp:35506). Items: dB des ersten Items, ohne Item
     „no item“.
  2. **Schreiben** bei Berührung (main.cpp:39341ff): einmal pro neuer Position
     (`sItemSentPos`), alle gewählten Items um dasselbe dB-Verhältnis. Kein Undo-Schritt, keine
     Automation (Item-Eigenschaft).
  3. **Motor** nach dem Loslassen (main.cpp:39450ff): folgt dem ersten Item, ohne Item bleibt er,
     wo er war (`sLastMotorPos`).
- **Berührung:** 150 ms entprellt (main.cpp:39121-39124); Motor schlaff beim Greifen
  (main.cpp:29325-29330). Fader 15 Bit, `kFaderMax = 0x7FFF` (UF1Protocol.h:37). Der Maler läuft
  im Takt des Kanalmalers, nicht pro USB-Ereignis.

#### Plan: ein Fader-Ziel pro Modus statt eines Hakens

`g_uf1JogFader[m]` (bool) wird `g_uf1JogFaderTarget[m]` (Auswahl), für **alle** Modi gleich
gebaut. Items' Haken wird dieselbe Auswahl mit „Track“ und „Item volume“; ExtState
`uf1JogFader2=1` lädt als „Item volume“, damit Franks Einstellung bleibt. Eine Stelle entscheidet,
was der Fader in diesem Modus fährt; Anzeige, Schreiben und Motor fragen sie, wie heute
`itemFader`.

Die Ziele, jedes mit Herkunft im Code:

| Ziel | Anzeige | Schreiben | Motor | Automation, Undo | Herkunft |
|---|---|---|---|---|---|
| **Track** (Werk) | wie heute | wie heute | wie heute | wie heute | Rest der Kette |
| **Item volume** | dB erstes Item, „no item“ | alle Items, gleiches dB-Verhältnis | erstes Item | keine, kein Undo | Items heute, unverändert |
| **Take volume** | dB aktiver Take erstes Item | aktive Takes, gleiches Verhältnis | erster Take | keine, kein Undo | neu, gleiches Muster mit `GetActiveTake` / Take-`D_VOL` |
| **Last touched parameter** | Wert wie Plug-in ihn formatiert, Name = Parametername | linear 0..1, Ziel beim Greifen festgehalten | liest den Parameter | Touch über `noteFaderFxEdit_` / `endFaderFxEdit_` wie Strip Mode | `applyLastParamStep_` (main.cpp:10620) für das Auffinden, `fromParam` (main.cpp:39290) für die Anzeige |
| **REAPER action (absolute)** | Prozent, Name = Kurzlabel der Action | `KBD_OnMainActionEx` mit 14 Bit (`val` = obere 7 Bit, `valhw` = untere 7 Bit, relmode 0) | bleibt, wo die Hand ihn liess, pro Modus gemerkt | was die Action tut | SDK reaper_plugin_functions.h:4084-4090; REAPERs OSC-Fläche schickt Absolutwerte genauso (csurf_osc.cpp:2757-2758) |
| **MIDI CC** | Wert 0..127 bzw. 0..16383, Name = Name des Controls | `midicc::messagesFor(Kind::Fader, …)`, 7 oder 14 Bit, an den Ausgang wie die CC-Ebene | gemerkter Wert, ohne Rückweg (siehe unten) | was das Ziel tut | MidiCcLayer.h:243, Ausgang `midiOutputIndexByName_` (main.cpp:23164) |

**Welche REAPER-Actions einen Wert nehmen** (gelesen aus REAPER 7.86 in /Applications, Texte im
Programm): rund 50 mit „(MIDI CC/OSC only)“ nehmen einen absoluten Wert, darunter „Track: Set
volume / pan / width / mute / solo for …“, „Adjust track FX parameter 01-16“, „Adjust last touched
FX parameter“, „Adjust track send 1-8 volume / pan“, „Set tempo, coarse / fine“, „Options: Set
metronome volume“, „View: Adjust horizontal / vertical zoom“, „View: Go to track“. Das sind die
Kandidaten für das Fader-Ziel „REAPER action“.

**Last touched wandert:** jeder Plug-in-Write von Rea-Sixty (UF8-V-Pots, UC1, Sticky Pot) stellt
REAPERs „zuletzt berührten“ Parameter um; der Code fängt das für die eigene Fokus-Verfolgung an
drei Stellen ab (main.cpp:22523, 44056, 44189). Darum hält der Fader beim Greifen fest, welchen
Parameter er fährt, und lässt ihn erst beim Loslassen los. Zwischen zwei Griffen folgt er wie
REAPER dem zuletzt berührten.

**Rückweg MIDI:** die CC-Ebene hat zwei Rückwege, die CC-Spuren der MIDI-Items auf der gewählten
Spur und einen Hardware-Eingang (main.cpp:40854-41026). Beide hängen an der aktiven UF8-CC-Ebene
(`g_ccIdx`, `g_ccLayer`) und an ihrem Zustand pro Seite und Streifen (`midicc::State`,
MidiCcLayer.h:360). Für einen einzelnen UF1-Fader passt das nicht ohne Umbau. Vorschlag: MIDI CC
ohne Rückweg, der Motor bleibt beim zuletzt gesendeten Wert.

Welche Ziele gebaut werden, ist Frage 5. „Track“ und „Item volume“ sind die Pflicht, weil Items
sie heute hat.

#### Regeln, die dabei gelten

- **Reihenfolge in den drei Ketten:** das Jog-Ziel bleibt vorne, wie Item volume heute. Es
  schlägt also auch Extender, MASTER, FLIP und Strip Mode. Steht das Ziel auf „Track“, läuft die
  Kette wie heute.
- **Linke Hälfte:** „Track“ löst über `uf1FaderTrack_` auf wie bisher. Die anderen Ziele hängen
  nicht an einer Spur (Items, Takes, zuletzt berührter Parameter, Action, MIDI), daher keine
  Verletzung der Hälften-Regel; die Zone sagt per Name, was der Fader fährt.
- **Name in der Zone:** bei allen Zielen ausser „Track“ steht dort, was der Fader fährt (wie
  „no item“ heute schon), sonst glaubt man, man fahre die Spur. 8 Zeichen.
- **Kein Ziel da** (kein Item, nichts berührt): Fader bleibt stehen, Name sagt es („no item“,
  „no param“), Schreiben tut nichts. Gleiches Verhalten wie Item volume heute.
- **Schreibtakt:** einmal pro neuer Position wie `sItemSentPos`. Für Action und MIDI wichtig, sonst
  geht pro Malertakt dieselbe Nachricht raus.
- **Moduswechsel während der Hand auf dem Fader ist:** das neue Ziel bekommt erst beim nächsten
  Greifen Werte. **Heute gelesen:** `itemFader` wird pro Malertakt neu bestimmt und der
  Schreibzweig fragt nichts anderes (main.cpp:39341-39421). Wechselt man von Items auf einen
  anderen Modus, während die Hand auf dem Fader liegt, schreibt der nächste Takt
  die Faderstellung des Items in das Ziel der normalen Kette (meist `CSurf_OnVolumeChange`): die
  Spurlautstärke springt. Umgekehrt
  springen alle gewählten Items auf die Stellung der Spur (`sItemSentPos` ist dann noch 0xFFFF).
  Das ist ein Fehler im heutigen Items-Fader, den der Umbau mit behebt: das Ziel wird beim Greifen
  festgehalten, wie oben beim Parameter. Nur aus dem Code gelesen, nicht am Gerät gesehen.
- **ORC-Side-Car, Hue Mode, RME-Seite:** diese Modi übernehmen den Schirm und den Fader vor dem
  Kanalmaler; das Jog-Ziel gilt dort nicht.
- **UF8:** unberührt, seine acht Fader bleiben Spuren.

#### Was der Plan nicht tut

Fader-Ziel als Wert für ein Built-in (Built-ins nehmen eine Schrittzahl, keinen Wert), Undo-Schritt
für Item- und Take-Lautstärke (heute keiner, Frage 6), Fader-Ziele auf dem UF8.

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
- `g_uf1JogFader`: geschrieben main.cpp:55244 und 61462, gelesen main.cpp:39281 und 55239,
  angeboten über `reasixty_uf1JogModeHasFader` (main.cpp:55233, SettingsScreen.cpp:5035). Alle
  vier Stellen wechseln auf das Ziel, keine bleibt beim bool.
- `itemFader` (main.cpp:39280) ist die eine Entscheidung, die Anzeige, Schreiben und Motor teilen.
  Sie wird zur Zielabfrage; drei getrennte Abfragen wären drei Kopien.

## Fallen

- **Undo:** viele REAPER-Actions legen pro Aufruf einen Undo-Schritt an. Ein Dreh am Jog sind
  schnell 50 Schritte. Nur im Handbuch nennen, nicht abfangen (REAPER entscheidet das pro Action).
- **Ketten mit Wartezeit** auf einer Jog-Bindung stauen sich beim schnellen Drehen in der
  Warteschlange. Vorschlag: der Editor sperrt Wartezeiten auf diesen Ids.
- **Nur Hauptbereich:** der Action-Picker ruft `PromptForAction(1, 0, 0)`, Bereich 0 = Hauptbereich
  (main.cpp:56538). Actions des MIDI-Editors kann man gar nicht wählen.
- **Welche REAPER-Actions relativ arbeiten** (REAPER 7.86, Texte im Programm): 16 mit „(MIDI CC
  relative/mousewheel)“ (Scroll und Zoom horizontal/vertikal, je auch umgekehrt, Seitenweise
  scrollen, Spurhöhe), „Transport: Scrub/jog (MIDI CC relative/absolute only)“, „Transport:
  Scrub/jog fine control (MIDI CC relative only)“, dazu „View: Move edit cursor“ und „View: Adjust
  horizontal scroll“ mit „relative recommended“. Rea-Sixty erkennt das nicht am Namen; eine andere
  Action bekommt bei Art 2 einen Wert und tut, was sie mit einem MIDI-Wert tut.
- **Tempo pro Raste** bestimmt bei relativen Actions REAPER, je Action anders. Der Teiler gleicht
  es aus. Wie gross ein Schritt pro Action ist, zeigt sich nur in REAPER.

## Nicht in diesem Plan

Eigene Encoder-Modi für den UF8 oder UC1 (Frage 4), eigene Anzeige pro Modus ausser dem Namen in
der Fader-Zone, Werksbeispiele.

## Geprüft und nicht geprüft

Gelesen in dieser Sitzung: alle Stellen mit Datei:Zeile oben, die SDK-Stellen zu
`KBD_OnMainActionEx` und relmode, REAPERs OSC-Fläche für relative und absolute Werte, die
Action-Texte im REAPER-7.86-Programm, die Companion-Lua, der Action-Picker, die Rückwege der
CC-Ebene, der Schreibzweig des Items-Faders beim Moduswechsel, die drei Stellen, an denen der Code
wandernde „last touched“-Parameter abfängt.

Nur in REAPER zu sehen, darum offen bis zum Bau: wie gross ein Schritt pro relativer Action ist,
und dass der Items-Fader beim Moduswechsel unter der Hand wirklich springt (aus dem Code gelesen).

## Fragen an Frank

1. Wie viele Plätze? Vorschlag 4.
2. Beide Arten anbieten (Links/Rechts und eine Aktion mit Richtung), oder nur eine?
3. SHIFT: eigener Platz mit Rückfall auf „Plain, feiner“, oder immer nur „feiner“?
4. Nur UF1, oder sollen dieselben Plätze auch als Encoder-Modi auf UF8 und UC1 wählbar sein?
5. Welche Fader-Ziele: Track und Item volume sind gesetzt. Dazu Take volume, Last touched
   parameter, REAPER action, MIDI CC: alle, einige, keins?
6. Item und Take volume über den Fader: ein Undo-Schritt pro Griff (beim Loslassen), oder wie
   heute ohne Undo?
7. Fader-Ziel auch für die eingebauten Modi (Playhead, Envelope, Fades …) wählbar, oder nur für
   Items und die eigenen?
