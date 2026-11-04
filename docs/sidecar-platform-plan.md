# SSL-Surface-Plattform: Core, zwei Hosts, Side-Cars

Stand 04.11.2026, Entwurf v2. **Nichts gebaut.** Zeilennummern beziehen sich auf upstream `main` `4b5d9343`
(04.11.2026). Der Fork-Code ist `m3lotunes/Rea-Sixty`, Branch `uad-console-mac`.

Herkunft:

- **[Code]**: im Repo gelesen.
- **[Doku]**: aus `docs/` im Repo.
- **[vermutet]**: nicht gemessen, steht unter „Zuerst messen“.

---

## 1. Das Bild

Bisher galt: Rea-Sixty ist das Produkt, und ORC ist die RME-App, die nebenbei die SSL-Hardware kann. Richtig ist
das Umgekehrte (Frank, 04.11.):

> **ORC ist ein DAW-loser Host für SSL-Surfaces.** Rea-Sixty ist der REAPER-Host für dieselben Surfaces.
> RME/TotalMix, MoreMe, Hue, DynaMount, OBS und UAD sind Side-Cars, die in beiden laufen.

```
┌──────────────────────────── Side-Cars ────────────────────────────┐
│  TotalMix   MoreMe   Hue   DynaMount   OBS   UAD   (… weitere)     │
└───────────────▲───────────────────────────────▲───────────────────┘
                │ sidecar::SideCar              │
┌───────────────┴──────────┐       ┌────────────┴──────────────────┐
│ ORC (standalone Host)    │◄─────►│ Rea-Sixty (REAPER-Host)       │
│ Bindings, Settings-Web,  │Hand-  │ Bindings, ImGui-Settings,     │
│ Tray, Phones             │over   │ REAPER-Layer                  │
└───────────────▲──────────┘       └────────────▲──────────────────┘
                └────────────── Core ───────────┘
   UF8/UC1/UF1-Treiber, Protokoll, Uf8Frames, Uf8Strips, Uf8Fader,
   Side-Car-Registry, Bindings-Kern, Handover
```

REAPER ist in diesem Bild **ein Ziel unter vielen**. Es ist nur das einzige, das wegen seiner API im fremden
Prozess laufen muss, und darum ist Rea-Sixty ein eigener Host. Der Handover wechselt den Host, nicht das Produkt.

### Der Code ist schon halb so

| Was | Wo | Stand |
|---|---|---|
| UF8-Frames, Fader, Strips | `Uf8Frames.h`, `Uf8Fader.h`, `Uf8Strips.h` | in beiden Hosts genutzt [Code: CMakeLists 815–828] |
| TotalMix auf der UF8 | `Uf8Rme.h` | reine Logik, ohne REAPER, in ORC und Rea-Sixty [Code] |
| Hue/DynaMount auf einem Strip | `Uf8ModeViews.h` | in ORC (`orc/Uf8Surface.cpp:306 ff.`) und Rea-Sixty [Code] |
| Handover | `OrcHandover.h` | Dateien in `~/Library/Application Support/ORC`, Protokoll v2 [Code] |
| ORC-eigene Surfaces | `orc/Surface.h`, `orc/Uf8Surface.cpp` | eigener Host-Code [Code] |
| UAD | Fork, `UadSurface` | in sich geschlossen, `fromUf8(Frame) → Out`, mit Simulatoren getestet [Code] |

Auf der Rea-Sixty-Seite fehlt die Trennung: Side-Cars sind dort Auswahlmodi.

- `SelectionMode` enthält `DynaMount`, `Hue` und `TotalMix` (`main.cpp:2319`).
- Rund 50 Stellen in `main.cpp` fragen diese Modi ab: `onUf8InputUnit_` (27453 ff.), `pushZonesForVisibleSlots`
  (41198 ff.), `reaperColorForVisibleSlot` (39697), `pushVuMeter` (45002), `sideCarKeys_` (2341),
  `registerBindingHandlers` (57366).
- `Uf1SideCar { None, RmeMonitor }` hat einen eigenen Weg (`main.cpp:935`, 15 Stellen).
- `DynamicBankKind` enthält `HueScenes`, `ObsScenes`, `RmeSnapshots` und `RmeLayouts` (`Bindings.h:719`).
- Die Settings-Tabs sind handgeschrieben: `SettingsScreen.cpp:23909` (DynaMount), 24158 (Hue).

Folge: Jedes neue Gerät fasst dieselben fünf Dateien an, und jeder Fork kollidiert dort.

---

## 2. Die drei Ebenen

### 2.1 Core (`extension/core/`, eine statische Library)

Alles, was beide Hosts brauchen und was REAPER nicht kennt:

- Gerätetreiber `UF8Device`, `UC1Device`, `UF1Device` und die Protokolle
- `Uf8Frames`, `Uf8Strips`, `Uf8Fader`, `Uf1Text`, `Uf1Pacer`, Paletten
- **Eingabe-Auswertung**: rohe Frames werden zu `sidecar::Input` (Abschnitt 3.2), inklusive Modifier, Long-Press und
  Doppelklick
- **Bindings-Kern**: Bänke, Sets, Gesten. Heute steckt er in `Bindings.cpp` und ist mit REAPER-Aktionen verwoben. Die
  REAPER-Aktionen bleiben in Rea-Sixty, der Rest wandert in den Core, damit ORC dieselben Gesten bekommt.
- Side-Car-Registry und Handover (`OrcHandover.h` plus SSL-360-Handover, Abschnitt 5)

### 2.2 Hosts

| | ORC | Rea-Sixty |
|---|---|---|
| Läuft als | eigene App (Tray, macOS/Windows) | Extension im REAPER-Prozess |
| Hat die Hardware | wenn keine DAW sie will | solange REAPER läuft |
| Settings | Web-UI (`SettingsServer`, `OrcWebWindow`) | ImGui (`SettingsScreen`) |
| Eigene Layer | keine, nur Side-Cars | REAPER-Layer, MIDI-CC-Layer, plus Side-Cars |
| Speichern | ORC-Config | REAPER ExtState / Projekt |

Beide implementieren `sidecar::Host` (Abschnitt 3.1). Weil die Settings in den Hosts verschieden gebaut sind (Web
gegen ImGui), beschreiben Side-Cars ihre Settings **deklarativ**, und jeder Host zeichnet sie selbst
(Abschnitt 3.4). Das ist hier keine Kür mehr, sondern Pflicht.

### 2.3 Side-Cars (`extension/sidecars/<id>/`)

Pro Side-Car ein Ordner, eine Klasse, ein Registrierungsaufruf und eine CMake-Option (`-DREASIXTY_SIDECAR_UAD=ON`).
Ein Side-Car kennt **keinen** der beiden Hosts, nur `Host&`.

---

## 3. Das Interface

### 3.1 Host

```cpp
namespace reasixty::sidecar {

class Host {
public:
    virtual ~Host() = default;
    virtual const char*  hostId() const = 0;                 // "orc" | "reasixty"
    virtual std::int64_t nowMs() const = 0;
    virtual void requestRepaint() = 0;
    virtual void banner(const std::string& text) = 0;        // Mode-Banner / UF1-Zeitfeld
    virtual std::string config(const std::string& key) const = 0;           // Namespace = SideCar::id()
    virtual void        setConfig(const std::string& key, const std::string& v) = 0;
    virtual std::uint32_t paletteRgb(int idx) const = 0;
    virtual void log(const std::string& line) = 0;
    // Optional: was nur Rea-Sixty hat (Marker-Cues, Recording-Status für das Hue-Aufnahmelicht).
    virtual class DawInfo* daw() { return nullptr; }         // ORC: nullptr
};
```

`daw()` ist der einzige Ort, an dem ein Side-Car von einer DAW erfährt. Hues Aufnahmelicht und OBS-Marker
brauchen das. In ORC gibt es `nullptr`, und der Side-Car lässt diese Teile weg.

### 3.2 Eingaben: ausgewertet, nicht roh

```cpp
enum class Control : std::uint8_t { Fader, FaderTouch, VPot, VPotPush, Sel, Cut, Solo,
                                     SoftKey, SendPlugin, Channel, ChannelPush, NavCross, Jog };
enum class Gesture : std::uint8_t { Press, Release, Long, Double };
enum class Surface : std::uint8_t { Uf8, Uc1, Uf1 };

struct Input {
    Surface surface;
    Control control;
    int     strip = -1;          // 0..7, immer relativ zum Side-Car
    int     index = -1;          // Soft-Key 0..7, SEND/PLUG-IN 1..8, Cross-Richtung
    double  value = 0.0;         // Fader 0..1, V-Pot Detents, Taste 1/0
    Gesture gesture = Gesture::Press;
    bool    shift = false, flip = false;
};
```

Der Fork nimmt heute rohe Frames (`UadSurface::fromUf8`). Ausgewertete Eingaben sind besser:

- Side-Cars kennen das SSL-Protokoll nicht. Ändert SSL es per Firmware, ändert sich nur der Core.
- Gesten und Modifier werden einmal für alle ausgewertet, und zwar in beiden Hosts gleich.
- Zwei UF8 und der UF1-Extender sind Sache des Hosts. Der Side-Car sieht Strips 0..7.

### 3.3 Side-Car

```cpp
struct Strip {
    uf8strips::View view;        // bestehender Typ
    double fader01 = 0.0, ring = 0.0;
    std::uint8_t vuL = 0, vuR = 0;
    bool owned = true;           // false = Host zeigt hier weiter seinen eigenen Inhalt
};
struct Key  { std::string label; std::uint32_t rgb = 0; Brightness lamp = Brightness::Off; };
struct Caps { bool uf8 = true, uf1 = false, uc1 = false, ownSoftKeys = true, meters = false; };

class SideCar {
public:
    virtual ~SideCar() = default;
    virtual const char* id() const = 0;            // "totalmix", "moreme", "hue", "dynamount", "obs", "uad"
    virtual const char* name() const = 0;
    virtual Caps caps() const = 0;

    virtual void start(Host&) = 0;                 // Socket, OSC, Bridge
    virtual void stop() = 0;
    virtual bool available() const = 0;            // TotalMix-Settings da? Apollo online?
    virtual void service() = 0;                    // Timer, nie blockierend
    virtual void enter() {}                        // übernimmt die Fläche
    virtual void leave() {}

    virtual Strip strip(int s) = 0;
    virtual Key   softKey(int k) { return {}; }
    virtual bool  onInput(const Input&) = 0;       // true = verbraucht

    virtual class Uf1Side* uf1() { return nullptr; }
    virtual std::vector<Action>        actions()  { return {}; }   // bindbar in beiden Hosts
    virtual std::vector<BankProvider*> banks()    { return {}; }   // ersetzt HueScenes, ObsScenes …
    virtual SettingsSchema             settings() { return {}; }
    virtual std::string exportState()  { return {}; }              // für den Handover (3.5)
    virtual void        importState(const std::string&) {}
};

void      registerSideCar(std::unique_ptr<SideCar>);
SideCar*  active();                                // nullptr = Host-eigener Inhalt
void      setActive(SideCar*);                     // mit Rückweg zum Zustand davor

} // namespace reasixty::sidecar
```

**`service()` gegen `active()`:** Ein Side-Car kann gestartet sein, ohne die Fläche zu haben. Das Hue-Aufnahmelicht
und OBS laufen im Hintergrund weiter. `service()` läuft für alle *gestarteten* Side-Cars, `strip()` und `onInput()`
nur für den *aktiven*.

### 3.4 Settings deklarativ

```cpp
struct Field {
    enum Kind { Text, Number, Choice, Toggle, Colour, List, Button, Section } kind;
    std::string key, label, help;
    // Grenzen, Auswahlwerte, Callback für Button …
};
using SettingsSchema = std::vector<Field>;
```

ORC zeichnet das Schema im Web-UI, Rea-Sixty in ImGui. Für Sonderfälle wie Hues Bridge-Pairing oder DynaMounts
Kalibrierung gibt es `Button` mit Callback plus einen Status-Text.

### 3.5 Handover mit Zustand

Den Zustand von Hue, DynaMount und OBS übergeben die Hosts heute als Textzeilen (`OrcHandover.h`,
`handover.released` / `handover.returned`, `ModeSync.h`). Das Interface verallgemeinert das: Jeder Side-Car hat
`exportState()` und `importState()`, und der Handover schreibt pro `id()` einen Block. So kommt ein neuer Side-Car
ohne Änderung am Handover-Format mit.

---

## 4. Reihenfolge

1. **Core herauslösen.** Treiber, Frames und Strips sind fast schon getrennt (CMakeLists 815 ff.). Gebaut wird eine
   statische Library, die ORC und Rea-Sixty linken. Ein Verhalten ändert sich dabei nicht.
2. **Interface, TotalMix zuerst.** `Uf8Rme` ist schon rein. TotalMix muss in **beiden** Hosts über das Interface
   laufen, bevor der nächste Side-Car dazukommt.
3. **Eingabe-Auswertung und Bindings-Kern in den Core.** Das ist der grösste Brocken, weil `Bindings.cpp` REAPER-
   Aktionen und Gesten mischt. Danach hat ORC dieselben Gesten wie Rea-Sixty.
4. **DynaMount, Hue, MoreMe, OBS.** Die Anzeige liegt für zwei davon schon in `Uf8ModeViews.h`. Die Eingabe muss aus
   `onUf8InputUnit_` (27595, 28387, 28668) heraus.
5. **UAD aus dem Fork, gegen das Interface und als ORC-Side-Car zuerst.** Für Apollo-Besitzer ist der Wert, dass die
   UF8 Apollo-Mixer und der UF1 Monitor-Controller ist, **ohne dass eine DAW läuft**. Merge-Bedingungen: Rebase auf
   aktuelles `main`, den MIDI-CC-Umbau als eigener PR, m3lotunes als Code-Owner von `sidecars/uad/`.
6. **Bank-Provider.** `HueScenes`, `ObsScenes`, `RmeSnapshots` und `RmeLayouts` kommen aus `DynamicBankKind` heraus.
   Bestehende Bank-Sets, die heute nach Nummer adressiert sind (`kUf1SideCarBankSets`, `Bindings.h:911 ff.`), werden
   einmal auf `id()` umgeschrieben.
7. **SSL-360-Handover** (Abschnitt 5), unabhängig von 1–6 machbar, ist aber erst mit ORC als Haupt-Host sinnvoll.

Später, falls mehrere Leute Side-Cars ausserhalb des Repos bauen, kommen **ladbare Module**: eine C-ABI
(`rs_sidecar_v1`, Funktionszeiger-Struktur, Einstieg `ReaSixtySideCar(int abiVersion)`), Ablage in einem
`modules/`-Ordner, Verteilung über ReaPack. Mit der deklarativen Settings-Beschreibung aus 3.4 ist das ohne
Umschreiben machbar.

---

## 5. Handover an SSL 360°

### 5.1 Das Problem

SSL 360° belegt die USB-Schnittstelle jeder SSL-Surface exklusiv (`docs/user-manual.md:34`, `UF8Device.cpp:118 ff.`).
Wer Logic, Cubase oder Pro Tools mit 360° fährt, kann ORC heute nicht parallel nutzen. Ohne diesen Handover ist ORC
nur etwas für REAPER-User und für Betrieb ganz ohne DAW.

### 5.2 Die Idee

ORC hat die Hardware. Startet eine DAW, die 360° braucht, gibt ORC die Hardware frei und startet SSL 360°. Wird die
DAW beendet, beendet ORC SSL 360° und übernimmt die Hardware wieder. Für REAPER gibt es den Handover schon, nur geht
der zu Rea-Sixty statt zu 360°.

```
ORC hat HW ──(Logic startet)──► ORC lässt los ──► startet SSL 360° + Core ──► 360° hat HW
    ▲                                                                              │
    └── ORC übernimmt ◄── 360° + Core beendet ◄── ORC beendet 360° ◄──(Logic beendet)
```

### 5.3 macOS: machbar

- **Freigeben:** ORC schliesst die libusb-Handles, und das Gerät ist frei. Das geht heute schon beim REAPER-Handover.
- **DAW erkennen:** Mit `NSWorkspace`-Notifications (`didLaunchApplication` / `didTerminateApplication`) und einer
  Liste von Bundle-IDs in den Settings (Logic, Cubase, Nuendo, Pro Tools, Live, Studio One, LUNA, Bitwig). REAPER
  steht nicht auf der Liste, weil Rea-Sixty übernimmt.
- **Teil-Handover:** Laut Doku lässt sich in SSL 360° der UF1 einzeln deaktivieren, dann gibt 360° ihn frei
  (`docs/uf1-sidecar-rme-feasibility.md`, „Koexistenz mit SSL 360“, dort als *noch nicht selbst gesehen*
  vermerkt). Damit wäre ein **Hybrid** möglich: 360° fährt UF8 und UC1 für die DAW, ORC behält den UF1 als
  Monitor-Controller für TotalMix oder UAD. Für viele Setups ist das der eigentlich interessante Modus.

**Zuerst messen [vermutet]:**

1. Wie startet `SSL360Core`? Wenn er ein LaunchAgent mit `KeepAlive` ist, startet ihn launchd nach einem Kill neu.
   Dann muss ORC ihn mit `launchctl bootout` und `bootstrap` steuern statt mit `kill` und `open`. Prüfen mit
   `launchctl list | grep -i ssl` und `ls ~/Library/LaunchAgents /Library/LaunchAgents | grep -i ssl`.
2. Gibt `SSL360Core` die Geräte beim Beenden sauber frei, oder bleibt ein Claim hängen, bis man neu einsteckt?
3. **Reihenfolge-Rennen:** Die DAW startet schneller, als 360° seine V-MIDI-Ports anlegt (`SSL V-MIDI Port N`,
   [Doku: Fork mcu-layer-plan 2.2]). Erkennt Logic bzw. Cubase Ports, die erst nach dem Start auftauchen, als
   Control Surface? Sonst muss ORC 360° *vor* der DAW starten, also beim Klick auf das DAW-Icon. Das geht nur mit
   einem ORC-eigenen Starter („DAW über ORC öffnen“) oder einer Verzögerung.
4. Wie lange braucht 360° vom Start bis zur leuchtenden UF8? Das bestimmt, ob der Wechsel sich flüssig anfühlt.
5. Merkt sich 360° seine Layer-Zuweisung über einen Neustart? Ein harter Kill darf seine Einstellungen nicht
   zurücksetzen. Deshalb zuerst sauber beenden (AppleScript `quit`, SIGTERM) und erst nach Timeout härter.

### 5.4 Windows: kaum machbar

Unter Windows hängt der Zugriff nicht an 360°, sondern **am Treiber**: WinUSB (unser Weg) und SSLBUS (SSL)
schliessen sich auf demselben Gerät aus (`docs/windows-troubleshooting.md`, `docs/uf1-sidecar-rme-feasibility.md`).
Ein Handover müsste bei jedem Wechsel den Treiber umbinden. Dafür braucht es eine UAC-Abfrage, das Gerät meldet sich
neu an, und der heutige Installer bindet zudem alle drei IDs auf einmal um (`main.cpp:51406`). Das ist für einen
täglichen DAW-Wechsel unbrauchbar.

Der Hybrid (UF1 bei ORC, UF8/UC1 bei 360°) geht unter Windows dagegen einmalig: Nur der UF1 kommt auf WinUSB. Dafür
braucht es eine Installer-Variante mit **einer** Hardware-ID, wie in der Feasibility-Doku schon vermerkt.

### 5.5 Empfehlung

1. Zuerst den **Hybrid**: UF1 bei ORC, Rest bei 360°. Er braucht keinen Handover, nur die eingeschränkte
   Treiberbindung unter Windows, und er funktioniert auf beiden Plattformen.
2. Dann den **vollen Handover nur für macOS**, nach den fünf Messungen aus 5.3.
3. Windows bleibt beim Hybrid.

---

## 6. Offen

- **Bindings-Kern in ORC:** Wie viel vom Bindings-System soll ORC bekommen? Ganz (gleiche Gesten, gleiche Bänke)
  oder nur Gesten und feste Belegungen pro Side-Car?
- **Geteilte Auswahl:** Die TotalMix-UF8 teilt die Auswahl mit dem UF1 (`Uf8Rme.h`, „THE SELECTION IS THE UF1's“).
  Vorschlag: Die Auswahl gehört dem Side-Car, und UF8 und UF1 lesen dasselbe Objekt.
- **Zwei UF8:** Übernimmt ein Side-Car immer nur die linke UF8, oder darf er beide verlangen (`Caps`)?
- **Name:** Wenn ORC der Haupt-Host für SSL-Hardware ist, passt der Name dann noch zu dem, was es ist?
