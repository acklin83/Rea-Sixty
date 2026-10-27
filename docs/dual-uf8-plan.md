# Zwei UF8 als eine Fläche: Plan

Stand 27.10.2026, Entscheide von Frank eingearbeitet, Plan auf logische Fehler geprüft. Nichts gebaut. Grundlage: PR #10 von Tim Shadgett (tshadgett, `dual-uf8-pr`,
fünf Commits auf `4311c3e9`, getestet an seiner Kiste mit 2× UF8 + UF1 unter Windows 11).
Frank: „gemischter Weg“: den Geräteteil übernehmen, die Einbindung in die Fläche selbst bauen,
sein Diff als Karte. Frank hat kein zweites UF8: das Verhalten mit zwei Geräten kann nur Tim
am Gerät prüfen; Frank prüft nach jeder Stufe, dass mit einem UF8 nichts anders ist.

Rechtliches: MIT, kein CLA. Nach den GitHub-Nutzungsbedingungen (D.6) steht sein Beitrag unter
derselben Lizenz. Übernommener Code behält seine Autorschaft (Cherry-Pick) oder bekommt
`Co-Authored-By: Tim Shadgett <tshadgett@gmail.com>`.

---

## 1. Was der PR macht (gelesen)

- **Gerät (Stufe 1a, `5e45f266`)**: `UF8Device::open(unitIndex)` öffnet das N-te UF8, sortiert
  nach USB-Port-Pfad (Windows kann ein offenes Gerät nicht für die Seriennummer erneut öffnen).
  `countAttached()`, `portPath()`. `g_dev` = links, `g_dev2` = rechts, gleicher Lebenszyklus
  (Start, Wiederverbinden, später eingesteckt, Beenden). Action „Swap UF8 units“ (ExtState
  `uf8SwapUnits`). Rechts bleibt dunkel, sein Eingang wird verworfen.
- **Fläche (Stufe 1b, `d4beba42` + `6823772b`)**: ein Strip-Index 0..`uf8Strips_()`−1 (8 oder
  16), per-Strip-Zustand auf `kUf8MaxStrips` = 16, Rahmen an das Gerät des Strips
  (`uf8SendS_`, `sendLedFramesS_`), 8er-Rahmen (V-Pot-Ring und -Modus, VU, Auswahlmaske, GR,
  Farbbalken) pro Gerät, Eingang beider Geräte unter einem Mutex mit eigenem Rest-Puffer,
  geteilte Slots mit dem UF1 hinten angehängt (`uf8PanSlot_`, `uf8VolSlot_`, `uf8SendSlot_`),
  `g_dev2` als `shared_ptr` hinter Mutex, Helligkeit und Schlaf auch rechts, offene Edits beim
  Verschwinden des rechten Geräts abschliessen.
- **Modi (`89be6895`, `24f64280`)**: rechte Soft-Key-Reihe = nächste Seite bzw. nächste Bank
  des Quick-Sets; Plug-in Mode pro Map „Split“ (16-Kanal-Maps: beide Fader-Bänke) oder
  „Spread“ (zweite V-Pot-Bank rechts, `g_softKeyBankRight`); CC-Layer nur links.
- **Nur links**: Nav-Overlay, Encoder-Picker, DynaMount, Hue, Touch-to-Learn, CC-Layer.

## 2. Was im Review auffiel

1. **CC-Layer**: `ccPaintSurface_()` läuft statt `pushZonesForVisibleSlots()` und malt 8 Strips.
   Das rechte Gerät wird nicht gemalt, seine Fader, V-Pots und Tasten bedienen aber weiter die
   Spuren 9 bis 16. Unsichtbare Edits, stehendes Display.
2. **Dynamische Soft-Key-Bänke**: die rechte Reihe nimmt die nächste Bank, aber deren Slots 0..7.
   Die Lanes-Bank (übernimmt beide Reihen) zeigt rechts dasselbe wie links; FX-, Selset- und
   Favoriten-Listen laufen rechts nicht mit 9..16 weiter.
3. **Zweites UF8 suchen**: ohne `g_dev2` legt der Timer alle 5 s einen libusb-Kontext an und zählt
   die Geräte (`countAttached`), auf jedem Rig mit einem UF8, dauerhaft. Kosten nicht gemessen.
4. **Swap** ist nur eine REAPER-Action, kein Builtin (neue Actions sind bei uns immer auch ein Builtin).
5. **Basis `4311c3e9`**: ohne `a4c3cfbd` (Windows-`snprintf`), sein CI wird deshalb rot.
6. **UF1-Extender**: mit zwei UF8 17 breit, links (UF1 Slot 1, beide UF8 +1) oder rechts (Slot 17).
   Zwischen den Geräten nicht möglich: der Versatz gilt für die ganze Fläche
   (`stripToVisibleSlot`: `pos + bankOffset + uf8StripBase_()`).

## 3. Regeln (Entscheide Frank 27.10.2026)

- **Bindings**: ein UF8-Tab für beide Geräte. Die globalen Tasten beider Geräte lösen dieselbe
  Belegung aus. Eigene Belegungen fürs rechte Gerät gibt es nicht.
- **Links / rechts**: USB-Port-Reihenfolge ab Werk. *Settings → Devices*: je Gerät **Identify**
  (schreibt LEFT bzw. RIGHT auf die Scribbles) und **Swap**. Swap auch als Builtin und Action.
  Mit nur einem UF8 tut Swap nichts (Tims Action schloss und öffnete sonst das einzige Gerät).
- **Zweites UF8**: Einstellung „Second UF8“ in *Settings → Devices*, ab Werk aus. Nur wenn sie an
  ist, wird ein zweites Gerät geöffnet, beim Start und alle 5 s gesucht. Ein Rig mit einem UF8
  merkt nichts, auch nicht von Stufe 1.
- **Seiten auf zwei Geräten** (Soft-Key-Seiten, CC-Seiten): eine Seitentaste wählt die Seite des
  Geräts, auf dem sie gedrückt wird; das andere Gerät folgt mit p−1 bzw. p+1. Rechts steht immer
  die Seite nach der linken; hinter der letzten Seite ist die rechte Reihe leer. PAGE ◄ ► springt
  um 1. Jedes Gerät zeigt seine eigene Seite: die Seiten-LEDs (V-POT, 1..5) gehen pro Gerät, nicht
  mehr an beide gleich.
- **Dynamische Bänke**: nur FX und Lanes können mehr als 8 Einträge haben und werden am UF8 in
  8er-Seiten geblättert (`dynBankBankable_`). Zeigt links eine FX- oder Lanes-Bank, setzt die rechte
  Reihe ihre Liste fort (Einträge 9..16) und geblättert wird um 16. Alle anderen dynamischen Bänke
  haben fest 8 Einträge (Parameter Groups, Track Colours, Favoriten, CS/BC-Favoriten, Hue, RME,
  Selection Sets): rechts steht die nächste Bank des Sets (Tims Regel). Korrigiert 27.10., Frank:
  „welche Dyn Bank hat bei uns überhaupt 16 Einträge?“
- **CC-Layer**: rechts die nächste CC-Seite (Regel „Seiten“). Fader, V-Pots und Tasten des
  rechten Geräts gehören dann zu dieser Seite, Touch und Rückwege ebenso.
- **UF1-Extender**: links, Mitte, rechts. „Mitte“ (UF8 L | UF1 | UF8 R) wirkt nur mit zwei UF8,
  mit einem verhält sie sich wie rechts. Der Umschalter `uf1_extender_side` wird ein Dreierzyklus;
  die gespeicherten Werte 0 = links und 1 = rechts behalten ihre Bedeutung, 2 = Mitte kommt dazu.
- **Nur links**: Nav-Overlay, Encoder-Picker (auch wenn ENC PUSH am rechten Gerät gehalten wird),
  DynaMount, Hue, Touch-to-Learn. Das rechte Gerät malt dabei normal weiter. Steht im Handbuch.

## 4. Stufen

**Kein Release zwischen den Stufen 2 und 4.** Erst wenn Stufe 4 auf `main` ist, gehen zwei UF8 in
ein Release. Stufe 1 allein darf mit (Einstellung ab Werk aus).

### Stufe 1: Geräteteil übernehmen (Tims Code, seine Autorschaft)
Cherry-Pick von `5e45f266` auf `main`, Konflikte von Hand. Dazu von uns: die Einstellung
„Second UF8“ (gated Öffnen und Suchen), Swap als Builtin (Picker-Kategorie, Doku-Text) und als
No-op mit einem UF8. Ergebnis: mit der Einstellung an wird ein zweites UF8 geöffnet und bleibt
dunkel; aus, ist alles wie vorher. **Frank prüft: ein UF8 läuft unverändert, auch nach Ab- und
Anstecken, Einstellung aus und an. Tim prüft: zweites Gerät wird erkannt, Swap tauscht.**

### Stufe 2: Kern `Uf8Units.h` (reine Logik, ctest)
Strip ↔ (Gerät, lokaler Index), Slot-Helfer (Pan, Volume, Send) mit festen UF1-Slots, Bankbreite
und Strip-Verschiebung pro Strip mit Extender links / Mitte / rechts, Seitenregel (welche Seite
zeigt welches Gerät, Tastendruck je Gerät, Seite hinter der letzten), Blätterschritt der
FX- und Lanes-Bänke. Test: mit einem Gerät liefert jede Abbildung dasselbe wie die heutigen
Funktionen, Extender links und rechts eingeschlossen; „Mitte“ mit einem Gerät = rechts.

### Stufe 3: die Fläche (selbst gebaut, Tims 1b als Karte)
Stelle für Stelle durch seinen Diff von `d4beba42` und `6823772b`, jede gelesen, nicht blind
übernommen: Strip-Arrays auf 16, Senden an das Gerät des Strips, 8er-Rahmen pro Gerät,
ColorSync pro Gerät, Eingang beider Geräte, Lebenszyklus (rechts kommt und geht), Slots,
Settings → Devices (UF8 L / UF8 R, Identify je Gerät, Swap, Second UF8). Seine Review-Fixes
(Ownership von `g_dev2`, Helligkeit, offene Edits) gehören dazu.
**Der CC-Layer kommt in dieser Stufe mit**: mindestens ist das rechte Gerät auf dem CC-Layer dunkel
und ohne Funktion (kein unsichtbares Bedienen der Spuren 9..16); die nächste CC-Seite rechts folgt
in Stufe 4. **Frank prüft: ein UF8 unverändert (Fader, V-Pots, Sends, Plugin Mode, Extender
links/rechts, CC-Layer). Tim prüft: 16 Strips, Banking, Sends, Extender links/rechts.**

### Stufe 4: Modi auf zwei Geräten
Seitenregel für Soft-Keys und CC-Layer mit Seiten-LEDs pro Gerät, FX- und Lanes-Bänke fortsetzen und
um 16 blättern, CC-Layer rechts = nächste Seite (mit Touch und Rückwegen), Plug-in Mode Split /
Spread (Tims Lösung), UF1-Extender „Mitte“ mit Dreierzyklus. **Tim prüft am Gerät, Frank prüft
ein UF8 unverändert.**

### Stufe 5: Handbuch, Changelog, Antwort an Tim
Kapitel „Two UF8s“ (Einrichten, links/rechts, Seiten, was nur links läuft, Extender), Changelog
nennt Tim. Die Antwort auf den PR (Dank, gemischter Weg, Bitte um Tests je Stufe) geht schon vor
Stufe 1 raus: ich entwerfe, Frank liest, dann wird gepostet. Den PR schliessen wir, wenn Stufe 4
auf `main` ist.

## 5. Risiken

- **Ohne zweites Gerät kein eigener Test**: jede Stufe mit zwei UF8 hängt an Tim.
- **`main.cpp` wandert täglich**: Stufe 3 in einem Zug bauen, nicht über Tage verteilen.
- **Stufe 1 ändert das Öffnen auch für ein UF8**: statt `libusb_open_device_with_vid_pid` die
  Geräteliste plus `libusb_open`, unabhängig von der Einstellung. Unter Windows mit WinUSB
  ungeprüft auf Franks Kisten (vgl. Issue #9, Win10 WinUSB).
- **Seiten-LEDs pro Gerät**: heute gehen die globalen LEDs an beide Geräte gleich; die Seiten-LEDs
  brauchen einen eigenen Weg pro Gerät und eigenen Doppel-Sende-Schutz.
