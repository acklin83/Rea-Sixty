# Rea-Sixty — du bist endlich frei

*Ein Handbuch für REAPER-User, mit fiktivem User-Review hinten dran.*

Stand: 2026-05-18. Was hier steht, läuft. Was noch nicht läuft, steht
in Kapitel 14.

---

## 0. Worum es geht

Du hast eine SSL UF8. Vielleicht auch noch eine UC1 daneben. Du hast
sie gekauft, weil sie ordentlich gebaut sind, weil die Fader nicht
quietschen, und weil die Scribble-Strips Farbe können. Und dann hast
du sie an REAPER angeschlossen und gemerkt:

- Die Farben kommen nur an, wenn du auf jeden einzelnen Track ein
  SSL-Plug-in lädst. Bei 200 Tracks heißt das: 200 Plug-ins, nur damit
  die LCDs nicht grau bleiben.
- SSL 360° hält die USB-Schnittstelle exklusiv. Daneben darf nichts
  laufen. Also: alles oder nichts.
- Zwischen REAPER und SSL 360° hängt MCU. Eine Mackie-MIDI-Brücke aus
  den späten Neunzigern. 14-Bit-Fader. Keine Farbe. Keine Ahnung,
  welcher Plug-in-Parameter unter deinen Fingern ist.

Drei Mauern. Keine Bugs — so ist das Produkt gebaut. SSL muss fünf
DAWs gleichzeitig bedienen; was bei dir grindet, ist die Grenze
dieses Designs.

**Rea-Sixty löst SSL 360° auf der Host-Seite ab.** Eine REAPER-
Extension, eine Datei, kein Daemon, kein virtueller MIDI-Port, kein
Plug-in pro Track. REAPER redet über seine C-API mit der Extension,
die Extension redet über libusb direkt mit der UF8 / UC1. Fertig.

```
SSL 360°  : REAPER ─MCU MIDI─ SSL360Core (daemon) ─USB─ UF8/UC1
Rea-Sixty : REAPER ─C API────  Extension (in REAPER) ─USB─ UF8/UC1
```

Was du gewinnst:

- **Track-Farben überall, immer**, ohne Plug-in pro Track.
- **16-Bit-Fader** statt MCU-14-Bit.
- **UC1-GR-Meter** folgt der echten Kompression auf dem fokussierten
  Track, ohne SSL-Plug-in-Pflicht.
- **Sechs Selection Modes** (Norm, REC, REC + MON, AUTO, FX Cycle,
  Instance Cycle), alle direkt von der Surface erreichbar.
- **REC + RME (TotalReaper)** — Preamp-Gain, Phantom, Pad, Phase,
  Input-Channel-Switch direkt am V-Pot, mit Hardware-Input-Namen
  im Color-Bar.
- **88 eingebaute Rea-Sixty-Actions** plus der komplette
  REAPER-Action-Namespace — jeder Knopf, jeder Soft-Key, jeder
  V-Pot ist bindbar, inklusive Shift / Cmd / Ctrl × Short / Long.
- **FX Learn** für beliebige VST/JS/AU-Plug-ins, Name-Substring-
  Match, also überlebt Reorders.

Was du verlierst:

- Den SSL-360°-Daemon. Den brauchst du nicht mehr.
- Die Möglichkeit, parallel SSL 360° laufen zu lassen. Eines von
  beiden — Rea-Sixty oder SSL 360°. USB-Exklusivanspruch lässt nichts
  anderes zu.
- Ggf. deine SSL-Hardware-Garantie. Rea-Sixty schickt Bytes an die
  UF8 / UC1, die nicht Teil eines dokumentierten öffentlichen API
  sind. Wenn dir Garantie wichtiger ist als Farbe, lass die Finger
  davon. Details in `LICENSE` und im README-Abschnitt "Legal".

---

## 1. Was du brauchst

- REAPER (aktueller Build).
- macOS. Windows/Linux sind im Build-System angelegt, aber nicht
  smoke-getestet — siehe `ROADMAP.md` Phase 4.
- Eine SSL UF8 per USB-C. UC1 optional. UF8-only oder UC1-only ist
  auch ok.
- `libusb` aus Homebrew: `brew install libusb`
- **ReaImGui** via ReaPack. Ohne ReaImGui läuft die Hardware-Steuerung
  trotzdem, aber das Settings-Fenster erscheint nicht.
- **SSL 360° darf nicht laufen.** Auch der Hintergrund-Daemon
  `SSL360Core` muss aus sein (Activity Monitor → quitten). Sonst
  zeigt REAPERs Console eine Zeile mit `Rea-Sixty UF8:
  SSL360Core owns the device`.

---

## 2. Installieren (macOS)

```
brew install libusb
cd extension
cmake -B build -G "Unix Makefiles"
cmake --build build -j$(sysctl -n hw.ncpu)
```

Ergebnis: `build/reaper_rea-sixty.dylib`. Dann symlinken (empfohlen für
Entwicklung — Rebuilds werden nach REAPER-Neustart eingelesen):

```
ln -sf "$PWD/build/reaper_rea-sixty.dylib" \
       ~/Library/Application\ Support/REAPER/UserPlugins/reaper_rea-sixty.dylib
```

Oder kopieren:

```
cp build/reaper_rea-sixty.dylib \
   ~/Library/Application\ Support/REAPER/UserPlugins/
```

REAPER neu starten. Wenn alles ok ist, sind innerhalb eines
Display-Ticks die Track-Farben auf den Scribble-Strips. Wenn nicht,
in REAPER → View → Console schauen, dort steht der Grund. Siehe
Kapitel 11.

---

## 3. Quickstart (fünf Minuten)

1. REAPER → View → Console öffnen. Keine Meldung = gut. Eine Zeile
   `Rea-Sixty UF8: …` = Kapitel 11.
2. Ein paar Tracks anlegen, Farben vergeben. Die ersten 8 erscheinen
   auf den Scribble-Strips, gemappt auf die 16-Farben-Palette der
   UF8.
3. Einen Fader bewegen. REAPER-Volume folgt. Loslassen — Motor läuft
   auf REAPERs autoritativen Wert nach.
4. SEL auf einem Strip drücken. Der Track ist selektiert. UC1
   (falls verbunden) springt sofort auf das SSL-Plug-in dieses
   Tracks.
5. **360°-Taste** drücken. Settings-Fenster auf. Nochmal drücken: zu.
6. **BANK ▶** — 8 Tracks weiter. **BANK ◀** — zurück.

Das ist alles. Du brauchst kein Setup-Wizard, kein virtuelles MIDI-
Device, keinen Daemon.

---

## 4. Hardware-Tour

### 4.1 Pro Strip

**Fader.** 100 mm, motorisiert, touch-capacitiv. REAPER bekommt
echte Touch-Events — Automation-Modes, die zwischen "Finger drauf"
und "Finger weg" unterscheiden, funktionieren. Auflösung: 16 Bit.
Lässt du los, läuft der Motor auf den authoritativen REAPER-Wert,
auch wenn REAPER zwischendurch was geändert hat.

**V-Pot.** Push-Encoder über dem Fader. Drehung = signed Delta durch
das Bindings-System. Push = separater Button. Was er steuert, hängt
am aktiven Selection Mode und an der Page (Kapitel 5).

**Scribble-Strip.** Farb-TFT mit mehreren Zonen:

- Color-Bar oben (Track-Farbe oder, im REC + RME Mode, der
  Hardware-Input-Name).
- Track-Name.
- Value-Line.
- Im Plug-in Mixer Mode zusätzlich: Channel-Strip-Type-Label,
  Parameter-Name + Wert, Fader-dB-Readout, V-Pot-Bar, Channel-
  Nummer mit Folder-Sigil.

Track-Farbe wird per Closest-Match auf die 16-Eintragspalette der
UF8 gemappt. Ein paar Palette-Indizes sind noch nicht voll
kalibriert; off-palette REAPER-Farben snappen auf den nächsten
kalibrierten Nachbarn.

**SOLO / CUT / SEL.** Drei farbige LED-Tasten pro Strip. Folgen
REAPERs Solo / Mute / Select / Arm — mit Dim- und Hell-Varianten
für Automation-Modes. SEL kann optional Track-Farbe übernehmen. Beim
Start werden alle Per-Strip-LEDs hart auf dunkel gezogen, damit der
Firmware-Power-up-Zustand ("alles leuchtet") nicht in deine Session
durchblutet.

**Top Soft-Key** (über der Scribble). Pro Strip, frei bindbar.

### 4.2 Bank- und Page-Navigation

**BANK ◀ / BANK ▶.** Schiebt das 8er-Fenster in 8er-Schritten in
REAPER-Track-Reihenfolge.

**PAGE ◀ / PAGE ▶.** Page innerhalb des Banks, wo zutreffend.

**HOME.** Zurück auf Bank 0.

### 4.3 Channel-Encoder (rechts, groß)

Sechs bindbare Modes:

| Mode             | Effekt                                          |
| ---              | ---                                             |
| Standard         | Bank ±1 (Single-Track-Shift)                    |
| NAV              | Transport-Scrub                                 |
| NUDGE            | Playhead-Nudge                                  |
| FOCUS            | Mouse-Wheel-Emulation                           |
| Instance Cycle   | Cycle durch SSL-Instances auf dem fokus. Track  |
| FX Cycle         | Cycle durch ALLE FX auf dem fokussierten Track  |

FX vs. Instance: siehe `docs/concepts.md` und
`docs/instances-fx-handbuch.md`.

### 4.4 Layer-Tasten

**LAYER 1 / 2 / 3** links. Drei unabhängige Bindings-Maps. Wechseln
heißt: anderes Soft-Key-Set, andere Modifier, andere V-Pot-Defaults.
Pro Layer gibt es benannte Bank-Presets zum Snapshotten und
Recallen.

### 4.5 Soft-Key-Spalte

Top-Row über den Scribble-Strips und der rechte Soft-Key-Cluster —
alles bindbar, jeweils mit Plain / Shift / Cmd / Ctrl × Short / Long
Press.

### 4.6 SEND / PLUGIN-Reihe

Acht Tasten. Default-bindbar auf Sends 1..8, eine "Send-Layer"-View,
oder Plug-in-Routing. Vollständige Built-in-Liste in Kapitel 8.

### 4.7 NORM / REC / AUTO — Selection-Mode-Block

Die NORM-, REC- und AUTO-Hardware-Tasten sind bindbar wie jede
andere. Per Default schalten sie den globalen **Selection Mode**
(Kapitel 5.7). Unter Rea-Sixty sind alle sechs REAPER-Automation-
Modes direkt erreichbar; unter Stock-SSL-360° + REAPER-MCU ist AUTO
Pro-Tools-only und OFF hat keinen Effekt.

### 4.8 Transport, Zoom, 360°

Transport-Tasten gebunden auf REAPERs Play / Stop / Record / RW /
FF / Cycle. Rebindbar. Zoom-Cluster (5 Tasten) → REAPER Zoom V/H/Fit.
**360°-Taste** → Settings-Fenster auf/zu.

### 4.9 PLUGIN / CHANNEL

**PLUGIN.** Toggle: UF8 Plugin Mode (V-Pots steuern Plug-in-Params)
und SSL Strip Mode (Fader übernimmt CS-Level). Jeweils mit
"with-GUI"-Variante.

**CHANNEL.** Flippt zwischen Channel-Strip- und Bus-Compressor-
Domäne.

### 4.10 FLIP / PAN / FINE

- **FLIP** — Fader/V-Pot tauschen.
- **PAN** — V-Pots auf Pan zwingen.
- **FINE / SHIFT** — Shift-Modifier. Bindings mit Shift-Variante
  feuern, solange gehalten.

### 4.11 Foot-Switch-Jacks (FS1 / FS2)

**Noch nicht decoded.** Pedal stecken passiert nichts. Die Buchsen
nehmen die SSL-Factory-Funktion entgegen, wenn du SSL 360° lädst;
unter Rea-Sixty allein sind sie still.

---

## 5. Modes

### 5.1 DAW Mode

Default. Acht Strips = zusammenhängendes 8-Track-Fenster. Fader =
Volume, V-Pot = Pan (oder Layer-Default), Scribble = Farbe + Name.

### 5.2 Folder Mode

Action: **Folder Mode** auf eine Taste binden. Aktiv heißt: nur
Parent-Tracks füllen den Bank, Children sind unsichtbar bis du sie
expandierst. **Long-Press SEL zum Expand** ist noch nicht
verdrahtet (Phase 2.5a).

### 5.3 Show Only Selected

Komplement zu Folder Mode: filtert auf die aktuell selektierten
Tracks.

### 5.4 Plug-in Mixer Mode — Channel Strip

Hat der fokussierte Track Channel Strip 2 / 4K B / 4K E / 4K G oder
einen 360°-Link-CS-Wrapper, schaltet das LCD-Layout um:

| Zone       | Inhalt                                             |
| ---        | ---                                                |
| Top        | Soft-Key-Label — welcher Param am V-Pot liegt      |
| Darunter   | Channel-Strip-Type ("CS 2", "4K G", …)             |
| Darunter   | DAW-Track-Farbe                                    |
| Darunter   | Track-Name                                         |
| Darunter   | O/PdB-Fader-Readout                                |
| Darunter   | Selected-Param-Name + Wert                         |
| Unten      | V-Pot-Readout-Bar                                  |

Track-Farbe ist im Plug-in Mixer Mode unter Rea-Sixty **immer**
sichtbar. SSL 360°s "Plug-in Mixer UF8/UF1 SEL Keys"-Toggle gibt es
nicht — brauchst du nicht.

PLUGIN-Taste im Plug-in Mixer Mode togglet, ob Fader/Pan auf das
Plug-in oder den DAW-Track zielen. A/B-Compare und HQ-Mode der
Channel-Strip-Plug-ins sind als bindbare Actions verfügbar; beide
clickfree umgeschaltet (direkter VST3-State-Chunk-Patch, da die
Flags nicht über REAPERs Parameter-API erreichbar sind).

### 5.5 Plug-in Mixer Mode — Bus Compressor

Bei BC 2 oder 360°-Link-BC-Wrapper: Track-Position, Name,
MAKE-UPdB-Readout, GR-Meter, Selected-Param + Wert, V-Pot-Bar.

### 5.6 Hybrid Mode

In SSLs Welt: ein Layer ist Plug-in Mixer, der andere DAW, und du
musst aufpassen, dass die Banks nicht auseinanderdriften. Unter
Rea-Sixty gibt es genau einen Bank, geteilt über alle Modes — kein
Desync möglich.

### 5.7 Selection Modes

Sechs global-exklusive Modes, die SEL-Row und V-Pots gleichzeitig
umparken:

| Mode             | SEL-Row                                    | V-Pot Push          | V-Pot Rotation              |
| ---              | ---                                        | ---                 | ---                         |
| **Norm**         | Track-Select (weiß oder Track-Farbe)       | Track-Select        | Pan (oder Layer-Default)    |
| **REC**          | Record-Arm (dim rot / hell rot)            | Toggle `I_RECARM`   | Pan                         |
| **REC + MON**    | Wie REC                                    | Toggle `I_RECARM`   | Pan                         |
| **AUTO**         | Pro Automation-Mode farbig (Read/Write/…)  | Cycle Automation    | Step Automation             |
| **FX Cycle**     | Track-Select                               | Toggle FX-GUI       | Cycle ALLE FX auf dem Track |
| **Instance Cyc.**| Track-Select                               | Open Instance-GUI   | Cycle nur SSL-Instances     |

Anmerkungen:

- In REC / REC + MON ist die Track-Selektion auf der SEL-Row
  bewusst unsichtbar — die Row gehört dem Arm-State.
- In AUTO cycled SEL die Automation-Mode, V-Pot-Rotation stept
  zwischen Modes.
- FX Cycle ist breit (jedes Plug-in auf dem Track), Instance Cycle
  ist eng (nur learned SSL-Instances). Letzteres aktiviert die
  Instance-Bindings (SSL Strip Mode / UF8 Plugin Mode) wenn es auf
  eine erkannte Instance landet.

Builtins zum Binden:
`selection_mode_norm`, `selection_mode_rec`,
`selection_mode_rec_mon`, `selection_mode_auto`,
`selection_mode_instance` (= FX Cycle, alter Name für Backward-
Compat), `selection_mode_instance_cycle`.

### 5.8 REC + RME (TotalReaper)

Wenn der **REC + RME**-Master-Switch in Settings → Modes an ist und
du [TotalReaper](https://github.com/acklin83/totalreaper) gegen dein
RME-Interface laufen hast, bekommen REC und REC + MON eine zweite
Lage Verhalten obendrauf.

TotalReaper spiegelt TotalMix-FX-Zustand in per-Track ExtState-Keys
(`P_EXT:totalreaper_*`); Rea-Sixty liest die Keys und ruft die
TotalReaper-Named-Actions zurück.

**Master-Switch + Per-Button-Zuweisung** (Settings → Modes):

- **REC + RME** Master.
- **V-Pot Push Action** — None / Toggle 48V / Toggle Pad / Toggle
  Phase / Toggle Autolevel.
- **CUT Action** — gleiches Menü.
- **SOLO Action** — gleiches Menü.
- **V-Pot rotates preamp gain** — V-Pot-Rotation ±1 dB pro Rast.
- **Shift + V-Pot steps input channel** — schaltet `I_RECINPUT` um
  einen Step, Stereo bleibt Stereo, MIDI bleibt MIDI.

"None" hält die Strip-Default-REC-Behavior für diesen Button intakt.
Talkback, Routing Mirror und ähnliche globale TotalReaper-Actions
sind nicht im Per-Button-Menü (weil global, nicht per-Track) — binde
sie wie jede andere REAPER-Action auf einen Soft-Key.

**Color-Bar.** Im REC / REC + MON mit REC + RME zeigt die Color-Bar
den Hardware-Input-Namen des Strip-Tracks:

- Mono: as-is (`Mic 1`, `Line 3`, `MADI 5`).
- Stereo: als Paar (`MADI 5/6`) — Trailing-Number wird genommen,
  `n+1` angehängt.
- Zu lang für 7 Chars? RME-Präfixe werden gekürzt: `MADI ` → `MA `,
  `ANALOG ` → `AN `, `ADAT ` → `AD `, `SPDIF ` → `SP `, `AES ` → `AE `.
- User-Aliase ohne Trailing-Number (`Drums OH`): bleiben unverändert,
  ohne Paar-Suffix.
- Nicht-Hardware-Inputs (MIDI, Multichannel, "no input"): Zone zeigt
  Default-Text der Mode-Lage.

**Value-Line.** Zeigt Preamp-State:

```
   48V  Pd  Ph        12.5dB
```

Inaktive Flags rendern als Leerraum, damit die Position der
Indikatoren beim Toggeln nicht wandert. RME-Preamp-Gain ist immer
≥ 0, Readout droppt das Vorzeichen, clamped negativ auf 0. Wenn
TotalReaper noch keinen Gain-Wert für den Track gepushed hat:
`--dB` statt `0.0dB`. Gain-Quelle: `P_EXT:totalreaper_gain`-Cache,
den TotalReaper auf csurf-Enable per `/sendall` füllt — du startest
nicht bei 0 dB, du startest bei dem, was TotalMix schon hat.

**V-Pot / CUT / SOLO unter REC + RME:** feuern die zugewiesene
TotalReaper-Named-Action gegen den Strip-Track. Surface-Coalescer
wird beim Input-Channel-Switch kurz unterdrückt, damit Color-Bar,
Value-Line und SEL-State gemeinsam erscheinen statt in
Einzel-Frames zu reißen.

**Raus aus REC + RME:** NORM drücken, oder den Master-Switch in
Settings → Modes off. Ohne TotalReaper läuft Rea-Sixty auf den
normalen REC-Mode zurück — die Buttons feuern dann einfach nicht.

---

## 6. UC1

UC1 ist die parallele Surface. Folgt dem fokussierten REAPER-Track
unabhängig vom UF8-Bank: fokus = re-target.

### 6.1 Plug-in-Recognition

Gleiche Liste wie UF8 Plug-in Mixer Mode:

| FX-Name                        | Variante     | Domäne          |
| ---                            | ---          | ---             |
| Channel Strip 2                | CS 2         | Channel Strip   |
| 4K G / 4K E / 4K B             | 4K           | Channel Strip   |
| Bus Compressor 2               | BC 2         | Bus Compressor  |
| SSL 360 Link Bus Compressor    | Wrapper BC   | Bus Compressor  |
| SSL 360 Link                   | Wrapper CS   | Channel Strip   |

Mehrere CS-Instances auf demselben Track: Instance Cycle (Action).

### 6.2 Knob-Mapping

- **CS 2 / 4K** — 12 EQ-Knobs (LP, HP, vier Bänder mit Freq / Gain /
  Q), 7 Dynamics (Comp Threshold/Ratio/Release, Gate Threshold/
  Range/Release/Hold), 10 Buttons (Bell, Type, In). CS 2 hat
  bewusste Polaritäts-Inversionen auf LP, HMF/LMF Q, Comp/Gate
  Threshold — matched SSLs 360°-Verhalten; die 4Ks nicht.
- **BC 2** — 7 Top-V-Pots (Threshold, Makeup, Attack, Release,
  Ratio, HPF, Mix) plus IN.

### 6.3 GR-Meter

Built-in SSL-Plug-ins: Gain-Reduction wird über die PreSonus-VST3-
Extension `GainReduction_dB` gelesen, die REAPER für SSL Bus Comp /
Channel Strip Comp + Gate exposed. Kein JSFX-Probe, kein
Sidechain-Tap. Piecewise-linear-Kalibrierung: 6 Breakpoints für
den BC-VU-Motor (0/4/8/12/16/20 dB), 5 für die LED-Leiter und die
UF8-GR-Row (3/6/10/14/20 dB).

User-gemappte Plug-ins via FX Learn: GR-Source überschreibbar — pick
den VST3-Param, der GR trägt, Offset und Custom-Breakpoints im
FX-Learn-Editor.

Eine JSFX-Sidechain-Envelope-Follower-Probe ist im Repo
(`extension/jsfx/rea_sixty_gr_probe.jsfx`) als Fallback für
Compressors ohne `GainReduction_dB`. **Nicht** auto-inserted — manuell
auf den Track nach dem Compressor setzen und Sidechain routen.

### 6.4 LCD-Zones

Drei 3-stellige 7-Segment-Displays plus farbiger LCD-Bereich:
Header, Sub-Header, Value, Unit, Round-Indicator (Value-Arc),
Preset-Carousel (5-Slot, 14 Chars), Central-Control-Panel.

### 6.5 Brightness

Drei unabhängige Level — LEDs, Scribble-LCDs, GR/Status — alle in
Settings → Device.

---

## 7. Settings-Fenster

Action: **Rea-Sixty: Open / Close Rea-Sixty Settings**, default auf
der 360°-Taste. Dockbar. Themed sich auf REAPERs aktives Color-
Theme. Sechs Tabs.

### 7.1 Device

Pro Gerät: Connection-Status + Seriennummer. Controls:

- **Identify** — flasht das jeweilige Device (welches ist welches im
  Multi-UF8-Setup).
- **LED-Brightness** — 5 Stufen.
- **Scribble-LCD-Brightness** — 5 Stufen.
- **GR-Meter-Source** — Built-in `GainReduction_dB` oder FX-Learn-
  Override.
- **Track-Select-follows-Param** — Param am UC1 anfassen ⇒ Track
  selektiert.
- **Plug-in GUI follows Instance** — SSL-Plug-in-Fenster bleibt auf
  und re-targeted beim Instance-Cycle.
- **Pin position** — FX-Window-Position einfangen und für die
  nächste Session merken.

### 7.2 Bindings

Voller REAPER-Action-Picker plus Per-Control-Binding-Editor über
die Modifier-Matrix (Plain / Shift / Cmd / Ctrl × Short / Long).
Export/Import per-Layer JSON-Files.

On-Disk:

- macOS: `~/Library/Application Support/REAPER/rea_sixty/bindings.json`
- Windows: `%APPDATA%/REAPER/rea_sixty/bindings.json`

3 Layer, pro Layer 3 Quick-Slots × 6 Sub-Banks (V-POT + Soft 1..5) =
144 user-fillable Soft-Key-Slots, plus alle Global-, Per-Strip-,
Transport- und Soft-Key-Positionen. **Named Bank Presets** für
Snapshot/Recall ganzer Layer-States. Learn-Mode + Rechtsklick
Copy/Paste im Bindings-Editor.

### 7.3 Modes

Toggles für Folder Mode, Show Only Selected, per-Layer V-Pot-
Defaults, REC + RME Master + Per-Button-Belegung.

### 7.4 FX Learn

Siehe Kapitel 9.

### 7.5 Selection Sets

8 Slots pro Projekt. **UI ist da, Storage noch nicht** — Slot-Taste
markiert den Slot aktiv und lichtet die LED, aber die Store-und-
Recall-Logik kommt noch (Phase 2.5b).

### 7.6 About

Version, Build-Hash, REAPER- und ReaImGui-Version, Log-File-
Location, Repo-Links.

---

## 8. Bindings — was du binden kannst

Zwei Sorten Actions auf jedem Button / V-Pot / Soft-Key:

1. **Jede REAPER-Action** — der komplette REAPER-Action-Namespace,
   inkl. Custom-Actions und ReaScripts.
2. **88 eingebaute Rea-Sixty-Actions** — surface-spezifisch, kennen
   Banks, Focused-Tracks, Instances.

Gruppen:

- **Navigation** — Bank L/R, Page L/R, Home, Zoom Up/Down/L/R/Center.
- **Modifier** — Shift, Cmd, Ctrl (Momentary oder Toggle).
- **Channel-Encoder-Modes** — Nav, Nudge, Focus, Instance Cycle, FX
  Cycle, Mode Dispatch.
- **Track-Operations** — Selection Mode Normal, Selection Clear All,
  Select Relative, Tracks Arm All, Automation Zero All, Playhead
  Nudge, Mouse Scroll.
- **Instance / FX** — Instance Cycle / Next / Prev, FX Cycle, BC
  Track Scroll.
- **Strip-Modes** — SSL Strip Mode Toggle (± GUI), UF8 Plugin Mode
  Toggle (± GUI).
- **Plug-in-Control** — Show Focused Plug-in GUI, Bypass, Offline,
  Preset Next/Prev/Cycle, Show FX Chain.
- **Display** — Brightness LEDs / LCDs / Both Up/Down.
- **Surface-Filter** — Folder Mode, Show Only Selected,
  Selection-Set Recall 1..8.
- **Domain / Mixer** — Domain CS, Domain BC, Mixer Toggle, Pan
  Force, Flip.
- **Soft-Key-Utilities** — Softkey Bank Select, SSL Softkey, SSL
  Bank V-Pot.
- **Routing** — Send All 1..8, Send This, Receive This.
- **Automation** — Off, Read, Write, Trim, Latch, Latch Preview,
  Touch (per-Track + Global).
- **Escape-Hatch** — beliebige REAPER-Command-ID direkt feuern.

---

## 9. FX Learn

Settings → FX Learn. Mappt UF8- und UC1-Controls auf **beliebige**
Plug-in-Parameter — nicht nur SSL. ReaEQ, FabFilter, JS-Effects,
alles was REAPER hosten kann.

### 9.1 Was FX Learn pro Plug-in speichert

- Name-Match-String (Substring des FX-Namens).
- Domäne (Channel Strip / Bus Compressor / keine).
- UF8-Mode-Flag — alle 8 UF8-Strips treiben Params dieses Plug-ins.
- Per-Slot VST3-Param-Indices, optional Polarität invertiert.
- Optionale GR-Metering — welcher VST3-Param trägt GR, Offset in dB,
  zwei Kalibrier-Breakpoint-Tabellen.
- Optionale UF8-Bank- und Strip-Bindings.
- Snapshot der Param-Namen für die Scribble-Strips.

### 9.2 Wie Matching läuft

Substring-Match gegen den FX-Namen, wie REAPER ihn meldet:

- **Reorder** auf dem Track ist safe — der Match läuft pro Lookup
  und findet das Plug-in, wo immer es jetzt sitzt.
- **Rename** im FX-Fenster bricht das Mapping — nächster Lookup
  matched nicht mehr. Wenn du umbenennst, auch den Match-String in
  FX Learn updaten.

### 9.3 Wo die Daten liegen

`<REAPER_RESOURCE>/rea_sixty/user_plugins.json`. Atomic Save (Temp-
File → Rename), Crash beim Speichern kann die Datei nicht
korrumpieren.

---

## 10. Status-Messages (REAPER Console)

- `Rea-Sixty UF8: <reason>  (UF8 optional — continuing)` — UF8 nicht
  geöffnet. Häufigster Grund: SSL 360° hält das Device.
- `Rea-Sixty UC1: <reason>  (UC1 optional — UF8 continues)` — UC1
  nicht geöffnet. Extension läuft trotzdem auf UF8.

Files in `/tmp`:

- `reaper_uf8_midi_dests.log` — alle CoreMIDI-Destinations beim
  MIDI-Port-Open. Nützlich, wenn die MCU-Fallback-LED-Path nichts
  zeigt.
- Frame-Traces vom Frame-Trace-Action.

Firmware-eigene Strings (die UF8 schreibt selbst, bevor irgendein
Host handshaked — Rea-Sixty ersetzt sie beim Init-Replay):

- `UF8 Initialisation Complete` + `Awaiting Connection to SSL 360°
  Software`
- `Layer Set To None`
- `SSL 360° Connection Lost. Attempting to Reconnect`

Letzteres **nach** funktionierender Rea-Sixty-Session = Extension
ist mid-session gecrashed. Crash-Log suchen, REAPER neu starten.

---

## 11. Troubleshooting

**`SSL360Core owns the device`** — SSL 360° läuft noch. App quitten,
plus `SSL360Core` in Activity Monitor killen.

**Kein Settings-Fenster bei 360°-Taste** — ReaImGui nicht installiert.
ReaPack → ReaImGui → REAPER restart. Hardware-Steuerung läuft
trotzdem.

**Kein LED-Feedback auf Solo/Mute/Select/Arm** — Vendor-USB-LED-Path
läuft immer; der MCU-MIDI-Fallback hängt davon ab, dass CoreMIDI
einen Port mit "UF8" im Namen sieht. Beim Start schreibt Rea-Sixty
die MIDI-Destinations nach `/tmp/reaper_uf8_midi_dests.log`. Wenn
die Vendor-USB-LEDs gehen, ist der Fallback nur Kosmetik.

**Fader spinnt** — Actions **Toggle fader calibration logging** und
**Toggle fader input log** an, dann sehen, was wirklich rauskommt.

**LED falsche Farbe** — **Probe next global LED cell** läuft durch
die LED-Cells einzeln. Plus die parallele Action für die Legacy-
Mono-LEDs.

**GR-Meter falsch auf Drittanbieter-Compressor** — Settings → FX
Learn → den Compressor finden → GR-Override: VST3-Param picken,
Offset setzen, Kalibrier-Breakpoints anpassen, bis das On-Screen-
Meter zum Plug-in-eigenen Meter passt.

**Raw Bytes sehen** — Action **Frame trace** togglt ein
Frame-Log nach `/tmp`. Hilft, wenn eine Farbe oder LED falsch ist
und du gegen einen Capture vergleichen willst.

**Reset alles** — `~/Library/Application Support/REAPER/rea_sixty/`
löschen, REAPER neu starten. Extension legt Defaults wieder an.

---

## 12. Was diese Version kann

Aus dem laufenden Code gezogen. Fehlt was hier, ist es noch nicht
drin — siehe Kapitel 14.

**UF8 Outputs:** Track-Farben pro Strip, Upper/Lower Scribble-Text,
19-Char-Value-Line, Channel-Strip-Type-Label, Channel-Nummer mit
Folder-Sigil, V-Pot-Bar, Fader-dB-Readout, Selected-Strip-Bitmask.
Per-Strip SOLO/CUT/SEL Farb-LEDs mit Dim/Hell-Automation-Varianten.
Top-Soft-Key-LEDs. 40+ Global-Button-LEDs. VU-Meter-Pair. GR-Row im
Plug-in-Mixer-Heartbeat. Master-LED- + LCD-Brightness.

**UF8 Inputs:** Jeder dokumentierte Button. Fader bei 16-Bit. Fader-
Touch mit Debouncing. V-Pot-Deltas.

**UC1 Outputs:** GR bei 50 Hz. VU-Meter-Pair pro Strip. LED-Bus für
jeden Button. 7-Segment-Digits. Display-Zones: Header, Sub-Header,
Value, Unit, CS/BC-Kontextblöcke, Round-Indicator mit Value-Arc,
Preset-Carousel, Central-Control-Panel, Routing-Indicator,
Mode-Dots. Track-Name-Carousels (klein + groß). Drei unabhängige
Brightness-Level.

**UC1 Inputs:** Alle 31 Button-IDs. 16 V-Pots + 3 Encoders, signed
6-Bit-Deltas.

**REAPER-Integration:** `csurf_inst`. Bank-Window 8 Tracks in REAPER-
Reihenfolge. Selection-Follow auf `SetSurfaceSelected`. Folder-Mode
Parent-Only. Sechs Selection Modes. 88 Built-in-Actions plus voller
REAPER-Action-Namespace.

**RME / TotalReaper:** In REC und REC + MON eine optionale Lage
TotalReaper-Behavior — Hardware-Input-Namen, Preamp-Flags + Gain im
Value-Line, V-Pot/CUT/SOLO auf TotalMix-Toggles, Gain-Rotation,
Shift+V-Pot Input-Switch.

**Plug-ins:** CS 2, 4K B/E/G, BC 2, 360° Link (CS + BC Wrapper).
Substring-Recognition. Multi-Instance pro Track. A/B-Compare und
HQ-Mode via direktem VST3-State-Chunk-Patch (über REAPERs Param-API
nicht erreichbar).

**Settings-Window:** 6 Tabs, REAPER-Theme.

---

## 13. Quick-Reference — Button-Namen

| Per Strip 0..7        |                                |
| ---                   | ---                            |
| Fader                 | Touch, motorisiert             |
| V-Pot                 | Push-Encoder                   |
| Top Soft-Key          | Über Scribble                  |
| SOLO / CUT / SEL      | Farb-LED-Paare                 |

| Global                |                                                  |
| ---                   | ---                                              |
| LAYER 1 / 2 / 3       | Linke Kante                                      |
| 360°                  | Settings-Toggle                                  |
| BANK ◀ / ▶            | 8er-Bank                                         |
| PAGE ◀ / ▶            | Page innerhalb Bank                              |
| PLUGIN                | Plug-in-Mode-Toggle                              |
| CHANNEL               | CS/BC-Flip                                       |
| FLIP                  | Fader / V-Pot tauschen                           |
| PAN                   | V-Pots → Pan                                     |
| FINE / SHIFT          | Shift-Modifier                                   |
| NORM / CLEAR          | Selection-Mode Normal                            |
| REC / ALL             | Tracks Arm All                                   |
| AUTO / ZERO           | Automation Zero                                  |
| READ / WRITE / TRIM / LATCH | Automation-Modes                           |
| SEND / PLUGIN 1..8    | Send + Routing                                   |
| Soft 1..5 + V-POT     | Top-Right-Soft-Cluster                           |
| Channel-Encoder       | Nav / Nudge / Focus / Instance / FX Cycle        |
| Zoom-Cluster          | V/H-Zoom                                         |

---

## 14. Was noch nicht drin ist (ehrlich)

Damit du nicht in einem README-Marketing-Versprechen hängenbleibst:

1. **On-Screen Plug-in-Mixer-View.** Das dockbare Fenster trägt
   aktuell nur die Settings-Tabs. Der themed Mixer-View, der SSL
   360°s Plug-in-Mixer-Page spiegelt, ist auf der Roadmap (Phase
   2.6).
2. **GUID-keyed FX Learn.** README sagt "GUID-keyed". In der Praxis:
   FX-Name-Substring-Match. Konservativer (Reorder safe, Rename
   bricht). Verhalten korrekt, Doku-Wording lose.
3. **Selection-Set-Storage.** Slot-Taste lichtet die LED — Store-und-
   Recall noch nicht (Phase 2.5b).
4. **Auto-Insert der JSFX-GR-Probe.** Probe liegt im Repo, wird aber
   nicht automatisch eingefügt. Manuell setzen.
5. **Foot-Switch.** FS1 / FS2 USB-Event noch nicht decoded. Stecken
   passiert nichts.
6. **Cross-Platform-Builds.** Build-System hat macOS / Windows /
   Linux. Smoke-getestet ist nur macOS.
7. **Long-Press SEL → Folder Expand.** Folder-Mode filtert
   Parent-Only — Long-Press-Gesture zum Children-Aufklappen
   noch nicht verdrahtet (Phase 2.5a).
8. **In-App Firmware-Update.** Out of scope. SSL verteilt Firmware
   weiter über SSL 360°. Für seltene Firmware-Updates SSL 360°
   installiert lassen, danach wieder quitten.

---

## 15. Fiktives User-Review

> **★★★★★ — "Ich habe seit drei Wochen kein SSL-Plug-in auf einem
> Drum-Overhead-Track gesehen und es fühlt sich an wie Urlaub."**
>
> *— Tom, Mixing-Engineer aus Hamburg, 217-Track-Sessions, ungefähr*
> *zu viel Kaffee.*
>
> ---
>
> Ich gebe es zu — ich war kurz davor, die UF8 zu verkaufen. Nicht
> wegen der Hardware. Die Hardware ist großartig. Die Fader fühlen
> sich richtig an, die Scribble-Strips sind hell genug für mein
> nicht-ganz-dunkles Studio, und der Bauen-Job ist tadellos. Das
> Problem war die Software.
>
> Ich mixe hauptsächlich Pop und Rock-Produktionen. 150 bis 250
> Tracks pro Session ist normal. Ich color-code alles: Drums grün,
> Bass orange, Gitarren rot, Keys blau, Vocals gelb, Aux/FX violett.
> Es ist meine wichtigste Orientierungs-Hilfe. Und drei Wochen lang
> hat SSL mir gesagt: "Klar kannst du Farben auf der UF8 haben — leg
> einfach Channel Strip 2 auf jeden einzelnen Track." Ja. Auf 230
> Tracks. Während die CPU schon bei 70% läuft. Sicher.
>
> Ich habe dann angefangen, mir das Repo von Rea-Sixty anzuschauen,
> wie man jemandem zuschaut, der ein Auto zusammenbaut und du
> denkst: "Niemals fährt das." Vier Wochen später war ein
> .dylib-File da. Ich habe es in den UserPlugins-Ordner geschmissen,
> SSL 360° gequittet (inklusive dieses verfluchten SSL360Core-Daemon,
> der sich versteckt), REAPER neu gestartet, und die ersten 8 Tracks
> erschienen mit ihrer Farbe auf den Strips. Innerhalb einer Sekunde.
> Ohne ein einziges Plug-in. Ich habe wirklich, kurz, gelacht.
>
> Was seitdem passiert ist, in unsortierter Reihenfolge:
>
> - **Fader fühlen sich anders an.** Nicht in einer "ich-bilde-mir-
>   was-ein"-Art. In einer "ich-höre-keine-Stufen-mehr-in-langsamen-
>   Volume-Fades"-Art. Das ist der 16-Bit-vs-14-Bit-Unterschied. Ich
>   habe es nicht erwartet, dass mir das auffällt. Es ist mir
>   aufgefallen.
>
> - **REC + RME mit TotalReaper ist mein neues Lieblings-Feature.**
>   Ich habe ein Fireface UFX III. V-Pot drücken = 48V togglet
>   direkt am Preamp. V-Pot drehen = Gain. Shift + V-Pot drehen =
>   ich step durch die Hardware-Inputs durch und wechsle dabei
>   nicht das, was auf dem Strip steht — der Track-Name bleibt.
>   Das ist es, was ich seit Jahren von einer Recording-Surface
>   wollte und nie hatte.
>
> - **Selection Sets sind nicht fertig.** Die Slot-LEDs gehen an, die
>   Tracks werden nicht gemerkt. Das nervt mich, aber Kapitel 14
>   sagt es offen, und Phase 2.5b steht in der Roadmap. Ich kann
>   warten.
>
> - **Der Plug-in-Mixer-View im Fenster fehlt noch.** Das Fenster
>   trägt aktuell nur die Settings. Ich vermisse die SSL-360°-Optik
>   ehrlich gesagt nicht — ich gucke sowieso auf die Hardware.
>
> - **Folder Mode.** Ich habe meine Drums in einer Parent-Gruppe. In
>   Folder Mode sehe ich nur die Parents. Bank durch die Mix-
>   Struktur statt durch 230 einzelne Tracks. Long-Press SEL zum
>   Expand fehlt noch, aber ich habe es auf einen Soft-Key gelegt
>   und das tut für mich den gleichen Job.
>
> - **FX Learn auf einem FabFilter Pro-Q 4** — fünf Minuten Setup,
>   und ich habe vier UC1-Knobs auf Frequency/Gain/Q/Bandwidth der
>   ersten vier Bänder. Reorder im FX-Chain? Funktioniert weiter.
>   Rename? Bricht — aber das steht im Handbuch, ich war
>   vorgewarnt.
>
> - **Bug-Risk.** Das hier ist keine 1.0-Software einer Firma mit
>   QA-Abteilung. Es ist eine ehrliche, gut-dokumentierte Extension
>   mit einem klaren Architektur-Plan und einem Author, der die
>   Limitations im Handbuch selber aufschreibt. Das ist mir lieber
>   als ein 8-stelliger Auftritt auf einer Trade-Show, bei dem das
>   Feature nicht funktioniert.
>
> Würde ich das jemandem empfehlen? Wenn du REAPER-User mit
> SSL-Hardware bist und große Sessions fährst: **ja, sofort.** Wenn
> du Pro-Tools-User bist oder deine Sessions unter 50 Tracks haben:
> bleib bei SSL 360°, das tut dir nicht weh.
>
> Wenn dir die Hardware-Garantie wichtiger ist als alles andere: lass
> die Finger davon und wisse, dass du etwas verpasst.
>
> Mein Workflow ist anders geworden. Stiller. Weniger Klick auf "FX
> hinzufügen → Channel Strip 2 → speichern → nächster Track". Mehr
> Mixen. Es fühlt sich wieder so an, wie es sich anfühlen sollte. Wie
> als die UF8 noch ein Versprechen war, bevor SSL 360° dazwischenkam.
>
> Du bist endlich frei.

---

*Stand 2026-05-18. Wenn was nicht stimmt: Issue im Repo aufmachen
oder direkt einen PR — siehe `CONTRIBUTING.md`.*
