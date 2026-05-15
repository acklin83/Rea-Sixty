# Instances / FX — kurzes Handbuch

Stand: 2026-05-15 (Commit 54afc58, nach Revert der Unified-Plugin-Mode-Episode).

## Die zwei Begriffe

**Instance** — ein Plug-in, das Rea-Sixty direkt auf die Hardware mappt:
SSL Channel Strip 2 (+ 4K B / E / G), Bus Compressor 2, 360° Link, deren
Combos, und User-Plug-ins die du in FX Learn mit `uf8Mode` markiert hast.

**FX** — alles was REAPER's `TrackFX_GetCount` sieht. Jede Instance ist
eine FX, die meisten FX sind keine Instances.

Faustregel: *Auf die Hardware gemappt = Instance. Alles andere = nur FX.*

## Cycles — welche Aktion macht was

Drei Hardware-Punkte, zwei Cycle-Arten = sechs Actions:

| Surface              | FX Cycle (alle FX)                   | Instance Cycle (nur Instances)            |
| ---                  | ---                                  | ---                                       |
| V-Pot Sel-Mode       | `Selection Mode → FX Cycle`          | `Selection Mode → Instance Cycle`         |
| UF8 Channel Encoder  | `Encoder Mode → FX Cycle`            | `Encoder Mode → Instance Cycle`           |
| UC1 Encoder 2        | `FX Cycle` (frei bindbar Plain/Shift)| `Instance Cycle` (frei bindbar Plain/Shift)|

Plus die Button-Variante: jeder Button kann `fx_cycle` / `instance_cycle`
bekommen.

**Was passiert beim Cyclen:**

- *FX Cycle* schiebt den Cursor pro Track durch ALLE FX. Landet er auf
  einer Instance, wird die Instance automatisch zur "aktiven" Instance
  fürs CS/BC/UF8-Mapping (Sync via `syncInstanceFromFxIdx_`).
- *Instance Cycle* überspringt Nicht-Instances komplett und cycled nur
  zwischen CS, BC, 360° Link und UF8-only-User-Plug-ins.

## Plugin Mode — die vier Toggles

Nach dem Revert sind wieder zwei getrennte Modi mit je einer GUI-Variante:

| Action                              | Was passiert                                                                    |
| ---                                 | ---                                                                             |
| `ssl_strip_mode_toggle`             | UF8-Fader auf CS Fader Level der fokussierten CS-Instance. GUI bleibt zu.        |
| `ssl_strip_mode_toggle_with_gui`    | Wie oben, plus die CS-Instance-GUI öffnet sich (folgt bei Instance Cycle mit).   |
| `uf8_plugin_mode_toggle`            | Alle 8 UF8-Strips übernehmen die Params einer UF8-only-Instance (User-Plug-in).  |
| `uf8_plugin_mode_toggle_with_gui`   | Wie oben, plus die User-Plug-in-GUI öffnet sich.                                 |

**Wichtig:** Die beiden Modi sind ein Mutex — `ssl_strip_mode` parkt
`uf8_plugin_mode` und umgekehrt. Selection Mode wird beim Eintritt in
UF8 Plugin Mode geparkt und beim Exit restauriert.

Factory-Default auf dem PLUGIN-Button: plain = `ssl_strip_mode_toggle`,
Shift = `ssl_strip_mode_toggle_with_gui`. Frank hat das auf seinem Setup
zu `ssl_strip_mode_toggle_with_gui` (plain) / `uf8_plugin_mode_toggle`
(Shift) umgebunden.

## Plug-in Family — operiert auf dem aktiven FX

Diese Actions zielen auf den aktuellen FX-Cursor (was Cycle zuletzt
aktiviert hat); fällt zurück auf die fokussierte Instance wenn der
Cursor leer ist. Alle einer Action-Familie, alle frei bindbar:

| Action                  | Effekt                                                  |
| ---                     | ---                                                     |
| `plugin_bypass`         | Toggle Enabled. LED leuchtet wenn bypassed.             |
| `plugin_offline`        | Toggle Offline. LED leuchtet wenn offline.              |
| `plugin_preset_next`    | Ein Preset weiter (`TrackFX_NavigatePresets +1`).       |
| `plugin_preset_prev`    | Ein Preset zurück.                                      |
| `plugin_preset_cycle`   | Encoder-getrieben (signierter Delta) — UC1 Encoder 2 / freier Slot. |
| `plugin_move_up`        | FX im Chain einen Slot hoch (Cursor folgt mit).         |
| `plugin_move_down`      | FX im Chain einen Slot runter.                          |
| `show_focused_plugin_gui` | Öffnet / schliesst die GUI der fokussierten Instance. |
| `show_fx_chain`         | Toggle das FX-Chain-Fenster des Tracks.                 |
| `close_all_fx_guis`     | Zu auf allen Tracks (floating + chain).                 |

Alle in Settings → Bindings unter der Kategorie "Plug-in" zu finden.

## Selection Modes (SEL-Button-Verhalten)

Nicht zu verwechseln mit Plugin Mode — Selection Modes ändern was der
SEL-Button macht:

Selection Mode ändert auf welche Aktion der **SEL-Button** UND die
**V-Pot-Drehung** auf dem Strip mappen. Der V-Pot-Push macht in den
Cycle-Modi immer dasselbe: GUI der aktuellen Strip-FX auf/zu.

| Action                          | V-Pot dreht            | V-Pot Push        | SEL-Button         |
| ---                             | ---                    | ---               | ---                |
| `selection_mode_norm`           | Pan                    | Pan-Center        | Track selektieren  |
| `selection_mode_rec`            | Pan                    | Pan-Center        | Rec-Arm togglen    |
| `selection_mode_rec_mon`        | Pan                    | Pan-Center        | Rec-Monitor togglen|
| `selection_mode_auto`           | Automation-Mode-Delta  | Automation-Mode setzen | Automation-Mode-Step |
| `selection_mode_instance`       | **FX Cycle** auf dem Strip      | GUI der Strip-FX togglen | Track selektieren  |
| `selection_mode_instance_cycle` | **Instance Cycle** auf dem Strip| GUI der Strip-Instance togglen | Track selektieren |

In Settings → Bindings unter "Selection Modes". Display-Strings tragen
"(V-Pot)" / "(SEL Button)" Suffixe damit klar ist welche Hardware.

## Action Picker — wo finde ich was

Settings → Bindings → Rechtsklick auf eine Binding → "Edit" → Built-in
Combo. Search-Box oben (auto-fokussiert). Kategorien als TreeNodes:

1. **Cycle Actions** — Instance Cycle, FX Cycle, Instance Next/Prev
2. **Selection Modes** — SEL-Button-Verhalten
3. **Encoder Modes** — UF8 Channel Encoder Drehung
4. **Hardware Modes** — Flip, Pan, Plugin Mode Toggles
5. **Plug-in** — Bypass, Offline, Preset, Move, GUI
6. (…und der ganze Rest darunter)

## Decision Guide — "Ich will…"

| Wunsch                                              | Action                                        |
| ---                                                 | ---                                           |
| …die nächste Instance auf dem fokussierten Track    | `instance_cycle` mit param `+1`               |
| …das nächste FX (auch ReaEQ etc.) auf dem Strip     | V-Pot **drehen** in `selection_mode_instance`      |
| …die nächste Instance auf dem Strip (Strip-scoped)  | V-Pot **drehen** in `selection_mode_instance_cycle`|
| …GUI der aktuellen Strip-FX auf/zu                  | V-Pot **drücken** in Instance / InstanceCycle Mode |
| …Fader = CS Fader Level der fokussierten CS         | `ssl_strip_mode_toggle`                       |
| …8 Strips als Param-Editor für mein User-Plug-in    | `uf8_plugin_mode_toggle`                      |
| …aktive FX bypassen                                 | `plugin_bypass`                               |
| …GUI der aktiven Instance auf/zu                    | `show_focused_plugin_gui`                     |
| …alle FX-Fenster schliessen                         | `close_all_fx_guis`                           |
| …durch Presets der aktiven FX scrollen              | `plugin_preset_cycle` auf einen Encoder       |

## Querverweise

- `docs/concepts.md` — kanonische Doku inkl. Code-Symbol-Namen
- Settings → Bindings → Action Picker — alle Actions live
- Memory: [[terminology-fx-vs-instance]], [[plugin-mode-unified]]
