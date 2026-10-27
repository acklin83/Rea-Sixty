# Zwei UF8 als eine Fläche: Plan

Stand 27.10.2026. Nichts gebaut. Grundlage: PR #10 von Tim Shadgett (tshadgett, `dual-uf8-pr`,
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

## 3. Stufen

### Stufe 1: Geräteteil übernehmen (Tims Code, seine Autorschaft)
Cherry-Pick von `5e45f266` auf `main`, Konflikte von Hand. Dazu von uns: Swap auch als Builtin
(Kategorie „Surface“ o. ä., Picker-Kategorie, Doku-Text), und die Suche nach dem zweiten
Gerät nach Entscheid 4. Ergebnis: ein zweites UF8 wird geöffnet und bleibt dunkel, mit einem
UF8 ist alles wie vorher. **Frank prüft: ein UF8 läuft unverändert, auch nach Ab- und
Anstecken. Tim prüft: zweites Gerät wird erkannt, Swap tauscht.**

### Stufe 2: Kern `Uf8Units.h` (reine Logik, ctest)
Strip ↔ (Gerät, lokaler Index), Slot-Helfer (Pan, Volume, Send) mit festen UF1-Slots, Bankbreite
mit Extender links / Mitte / rechts und die Strip-Verschiebung pro Strip (für „Mitte“ rückt nur
das rechte Gerät). Test: mit einem Gerät ist jede Abbildung die Identität.

### Stufe 3: die Fläche (selbst gebaut, Tims 1b als Karte)
Stelle für Stelle durch seinen Diff von `d4beba42` und `6823772b`, jede gelesen, nicht blind
übernommen: Strip-Arrays auf 16, Senden an das Gerät des Strips, 8er-Rahmen pro Gerät,
ColorSync pro Gerät, Eingang beider Geräte, Lebenszyklus (rechts kommt und geht), Slots,
Settings → Devices „UF8 L / UF8 R“. Seine Review-Fixes (Ownership von `g_dev2`, Helligkeit,
offene Edits) gehören dazu. **Frank prüft: ein UF8 unverändert (Fader, V-Pots, Sends, Plugin
Mode, Extender links/rechts). Tim prüft: 16 Strips, Banking, Sends, Extender.**

### Stufe 4: Modi auf zwei Geräten
- Soft-Keys rechts = nächste Seite (Tims Lösung) und **dynamische Bänke laufen weiter** (Slots
  8..15 der Liste rechts), Entscheid 2.
- Plug-in Mode Split / Spread (Tims Lösung).
- CC-Layer auf beiden Geräten, Entscheid 1.
- UF1-Extender „Mitte“, Entscheid 3.
- Nav-Overlay, Encoder-Picker, DynaMount, Hue, Touch-to-Learn: links (Entscheid 5), das
  rechte Gerät malt dabei normal weiter.
**Tim prüft am Gerät.**

### Stufe 5: Handbuch, Changelog, Antwort an Tim
Kapitel „Two UF8s“; Changelog nennt Tim.

## 4. Entscheide für Frank

1. **CC-Layer mit zwei UF8**: (a) rechts = nächste CC-Seite (wie die Soft-Keys, Datenmodell
   bleibt), (b) rechts dunkel und ohne Funktion, solange der CC-Layer aktiv ist.
   Vorschlag: (a).
2. **Dynamische Bänke rechts**: Liste weiterführen (FX 9..16, Lanes 9..16 …). Vorschlag: ja.
3. **UF1-Extender „Mitte“** (UF8 L | UF1 | UF8 R) als dritte Seite. Vorschlag: ja.
4. **Zweites UF8 suchen**: (a) wie Tim, alle 5 s, (b) nur beim Start und per Action/Builtin
   „Look for a second UF8“, (c) alle 5 s, aber nur wenn in Settings „Second UF8“ an ist.
   Vorschlag: (c), Werk aus, damit ein Rig mit einem UF8 nichts davon merkt.
5. **Modi nur links** (Nav-Overlay, Picker, DynaMount, Hue, Touch-to-Learn): so lassen.
   Vorschlag: ja, später nach Bedarf.
6. **Tim**: Antwort auf den PR (Dank, gemischter Weg, Bitte um Tests der Builds je Stufe). Ich
   entwerfe, du liest, dann wird gepostet. Den PR schliessen wir, wenn Stufe 3 auf `main` ist.

## 5. Risiken

- **Ohne zweites Gerät kein eigener Test**: jede Stufe mit zwei UF8 hängt an Tim.
- **`main.cpp` wandert täglich**: Stufe 3 in einem Zug bauen, nicht über Tage verteilen.
- **Stufe 1 ändert das Öffnen auch für ein UF8**: statt `libusb_open_device_with_vid_pid` die
  Geräteliste plus `libusb_open`. Unter Windows mit WinUSB ungeprüft auf Franks Kisten (vgl. Issue #9, Win10 WinUSB).
.
