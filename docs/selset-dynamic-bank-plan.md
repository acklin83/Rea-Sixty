# Dynamische Bank „Selection Sets“ (UF8 und UF1): Plan

Stand 01.10.2026. **Gebaut** (SelsetBank.h, ctest selset_bank). Was über den Code dasteht, ist in dieser Sitzung nachgelesen
(Datei:Zeile).

## Was es heute gibt

- **Acht Slots** pro Projekt (oder global), `g_selsets` (main.cpp:2588). Zwei Arten:
  Snapshot (feste Spurliste, GUIDs) und Group (folgt REAPER-Gruppe 1–128).
- **`selset_recall`** Param 1–8: Slot aufrufen; derselbe Slot noch einmal = aus. Aufrufen ist
  ein **Filter für die Oberfläche** (`g_selsetActive`, `drainSelsets_`, main.cpp:3855): die
  Surfaces zeigen nur noch die Spuren des Sets, die Bank springt auf den Anfang. Die
  REAPER-Auswahl wird dabei **nicht** gesetzt. Mit Sel-Mode AUTO und gesetztem Auto-Mode
  schaltet der Aufruf zusätzlich den Automationsmodus der Spuren.
- **`selset_save`** Param 1–8: speichert die aktuelle REAPER-Auswahl als Snapshot in den Slot
  (`saveCurrentSelectionToSlot_`, main.cpp:3732). Überschreibt ohne Rückfrage, macht aus einem
  Group-Slot einen Snapshot, **setzt keinen Namen**.
- **Löschen und Benennen** nur in Settings → Selection Sets (`reasixty_selsetClear`).
- **Encoder:** Modus „Selset Cycle“ (UF8 und UF1-Ring) bzw. `selset_cycle` steppt aus → 1 → 2 …
- Auf Tasten heute nur von Hand: jede Taste einzeln mit Param belegen, Beschriftung selbst
  tippen, keine Anzeige, welcher Slot belegt ist.

## Die neue Bankart

`DynamicBankKind::SelectionSets`, hinten angehängt (nach `RmeLayouts = 11`, also 12;
`kDynamicBankKindLast` mitziehen, Bindings.h:685). Spurunabhängig: im Resolver vor der
`!tr`-Prüfung, wie Hue, OBS und RME (`dynamicBankSlot_`, main.cpp:8600).

**Belegung:** Taste N = Slot N. UF8: acht Top-Keys = acht Slots, kein Blättern. UF1: vier
Tasten, zwei Seiten, ◄ / ► lang blättert wie bei jeder dynamischen Bank.

**Beschriftung:**
- Name des Sets, gekürzt auf die Breite der Fläche.
- Belegt ohne Namen: „Set 3“.
- Group-Slot ohne Namen: „Grp 12“ (die REAPER-Gruppe).
- Leer: keine Beschriftung.

**Lampe nach der Side-Car-Regel:** hell = aufgerufen (Filter an), gedimmt = belegt, dunkel =
leer. Weiss; die Bank braucht keine Farben.

**Gesten** (die Bankart bekommt Modifier, `dynamicKindUsesModifiers`, Bindings.h:700; Gesten
0 Plain, 1 Shift, 2 Cmd, 3 Ctrl, 4 lang):

| Geste | Wirkung |
|---|---|
| Druck | Set aufrufen / wieder aus (wie `selset_recall`) |
| Lang | aktuelle REAPER-Auswahl speichern, **nur in einen leeren Slot** |
| Shift + Druck | aktuelle Auswahl speichern und **überschreiben** (auch einen belegten Slot) |
| Cmd + Druck | Spuren des Sets in REAPER **auswählen** (ohne Filter) |
| Ctrl + Druck | Slot leeren |

Warum so: Überschreiben und Leeren zerstören etwas, also nie auf der Geste, die man aus
Versehen auslöst (lang halten auf einem belegten Slot). „Laden“ heisst in Rea-Sixty heute
Filter; Cmd gibt das andere Laden dazu, das man zum Bearbeiten braucht: die Spuren wieder
ausgewählt haben.

**Rückmeldung beim Speichern:** Banner „Selection Set • 3 saved“ (bzw. „cleared“), auf dem
UF1 zusätzlich kurz „STORED 3“ im Zeitfeld (`uf1FlashTimecode_`; die 7-Segment-Schrift kann
K, M, V, W, X nicht, darum nicht „SAVED“). Die Lampe springt von dunkel auf gedimmt.

**Bankname:** „Selection Sets“ in Settings, kurz „SETS“ (`dynKindShort_`,
SettingsScreen.cpp:1374; UF1-Bankname ebenfalls „SETS“).

## Wo gebaut wird (die vier Stellen einer Bankart)

1. **Liste / UI** (SettingsScreen.cpp): Arten-Listen der UF8-Set- und Bank-Menüs und der
   UF1-Bank-Menüs (~5487, ~6654, ~7082, `kUf1DynOpts` ~8332), Name ~1330, Kurzname ~1374.
2. **Resolver:** `dynamicBankSlot_` (main.cpp:8600) liefert Label, „vorhanden“, Lampe aus
   `g_selsets` und `g_selsetActive`. UF1 über `dynamicBankSlotUf1_`.
3. **Ausführer:** UF8 `applyDynBankReq_` und UF1 `applyDynBankUf1_` rufen nur noch die
   vorhandenen Wege: Aufruf über `g_selsetActivateRequest`, Speichern über
   `saveCurrentSelectionToSlot_`, Leeren über dieselbe Funktion wie Settings
   (`reasixty_selsetClear`), Auswählen neu (Spuren des Sets per GUID selektieren). Keine
   zweite Kopie der Slot-Logik. Main-Thread, wie alle Bank-Ausführer.
4. **Anzahl / Blättern:** `dynamicBankItemCountUf1_` (main.cpp:8789) = 8. UF8 nicht bankbar
   (acht passen). ⛔ Die UF8-Seitenfelder `g_dynBankPage[8]` sind auf Art < 8 bemessen; da
   SelectionSets nicht bankbar ist, wird es nicht berührt, aber die Grenze bleibt im Kommentar
   stehen.

Dazu: Projekt-Tabs sind schon erledigt (`g_selsets` ist pro Tab, `g_selsetScoped`), die Bank
zeigt immer die Slots des aktiven Tabs.

**Test:** die reine Logik (Beschriftung, Lampe, welche Geste was darf) in einen kleinen Header
mit ctest: leer / belegt / Group / aktiv, lang auf belegt = nichts, Shift überschreibt.

**Handbuch:** Abschnitt Selection Sets und Dynamic Banks.

## Entschieden (Frank 01.10.2026)

1. Druck = Filter wie heute, Cmd + Druck = Spuren in REAPER auswählen.
2. Lang speichert nur in leere Slots, Shift + Druck überschreibt.
3. Ctrl + Druck leert den Slot am Gerät.
4. Speichern am Gerät setzt keinen Namen; angezeigt wird „Set 3“.
5. Keine eigene Werksbank: die Art steht im Bank-Menü wie jede andere dynamische Bank.
