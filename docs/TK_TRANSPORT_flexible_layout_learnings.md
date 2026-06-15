# TK_TRANSPORT — Learnings für ein flexibles Focused-Track-Panel

Studie von `TK Scripts/Transport/TK_TRANSPORT.lua` (TouristKiller, v2.0.0, ~13.7k LOC)
plus Submodule `custom_buttons.lua`, `button_renderer.lua`, `button_editor.lua`.
Ziel: dieselbe „geil flexible" Konfigurierbarkeit auf unser Focused-Track-Panel übertragen.

Datum: 2026-06-15

---

## TL;DR — die 7 Ideen, die wir klauen sollten

1. **Dual-Koordinaten pro Element** (`_x`/`_y` als Fraction 0..1 **und** `_x_px`/`_y_px`). Pixel ist während Drag führend, Fraction wird daraus synchronisiert → resolution-independent + responsiv.
2. **Edit-Mode mit Drag-to-Move + Grid-Snap** (zwei Snap-Modi: `step` vs `magnetic`), Alignment-Guides, optionales Snap an REAPERs native Transport/TCP-Fenster.
3. **Flache Settings-Tabelle** (ein einziges `settings`-Table, ~471 Keys, Naming-Convention statt Objekt-Hierarchie). Trivial zu serialisieren.
4. **Presets als JSON-Dateien** in einem Ordner + „last used"-Pointer in ExtState. Save/Load = `for k,v in pairs(settings)` → JSON.
5. **Data-driven Button-Rendering**: ein `button_order`-Array + ein `BTN_DEFS`-Table treiben eine einzige Render-Schleife. Reordern = Array umsortieren.
6. **Mode-Suffix-Pattern** (`_text`/`_graphic`/`_custom`): identische UI- und Render-Logik, nur Key-Suffix wechselt → drei komplette Darstellungs-Sets gratis.
7. **Z-Order als User-Setting** (`z_order_<element>` ints, beim Rendern sortiert) → User bestimmt Layering per Up/Down-Arrows.

---

## 1. Positionierungs-System (das Herzstück)

### Naming-Convention pro Element
```
<name>_x        Fraction 0..1   (kanonisch / gespeichert)
<name>_y        Fraction 0..1
<name>_x_px     absolute Pixel  (führend während Drag)
<name>_y_px     absolute Pixel
```

### Dual-Koordinaten: warum beides?
- **Fraction** = resolution-/window-size-unabhängig. Beim Resize bleibt das Element relativ an Ort.
- **Pixel** = präzise, ruckelfrei beim Draggen.
- Regel: **Pixel ist während Bewegung die Quelle der Wahrheit, Fraction wird daraus abgeleitet.**

`Layout.move_pixel(dx_px, dy_px, _, _, keyx, keyy, max_w, max_h)`:
```lua
-- fehlende Werte lazy initialisieren (Fraction default 0.5 = Mitte)
if settings[keyx]    == nil then settings[keyx]    = 0.5 end
if settings[px_keyx] == nil then settings[px_keyx] = floor(settings[keyx] * max_w) end
-- Pixel-Delta anwenden (geclamped; Y darf bis -100 für „über dem Fenster")
settings[px_keyx] = clamp(settings[px_keyx] + dx_px, 0, max_w)
settings[px_keyy] = clamp(settings[px_keyy] + dy_px, -100, max_h)
-- Fraction aus Pixel zurücksynchronisieren  ← der Trick
settings[keyx] = settings[px_keyx] / max(1, max_w)
settings[keyy] = settings[px_keyy] / max(1, max_h)
```

### Beim Rendern: Pixel bevorzugen, sonst Fraction
```lua
local x = settings.foo_x_px and ScalePosX(settings.foo_x_px, win_w, settings)
                             or ((settings.foo_x or 0.5) * win_w)
ImGui_SetCursorPosX(ctx, x)
```

### ScalePosX/Y — optionales responsives Skalieren
Wenn `*_scale_with_width` aktiv ist, wird der gespeicherte Pixelwert gegen eine
Referenzbreite skaliert: `px * (current_w / ref_w)`. Sonst 1:1. So können Pixel-Layouts
auch bei Window-Resize mitwachsen, ohne dass man auf Fraction wechseln muss.

### Bounding-Box-Registry (für Hit-Test / Edit-Mode)
Direkt **nach** dem Rendern eines Elements:
```lua
local function StoreElementRect(name)
  local x1,y1 = ImGui_GetItemRectMin(ctx)
  local x2,y2 = ImGui_GetItemRectMax(ctx)
  element_rects[name] = {min_x=x1,min_y=y1,max_x=x2,max_y=y2}
end
```
`element_rects` (global Table) wird im Edit-Mode für Maus-Hit-Test und Overlay-Zeichnen
genutzt. Für zusammengesetzte Elemente: `StoreElementRectUnion(name, x1,y1,x2,y2)` manuell.

---

## 2. Edit-Mode

State-Machine über drei Globals:
- `overlay_drag_active` = Name des gerade gezogenen Elements (oder nil)
- `overlay_drag_last_x/y` = letzte Mausposition
- `overlay_drag_moved` = Dirty-Flag (nur dann beim Loslassen speichern)

Ablauf pro Frame (`RenderEditModeOverlays`):
1. Maus-Klick im Rect eines Elements → drag start, `last = mouse`.
2. `dx = mouse - last` berechnen.
3. **Snapping:**
   - `step`: rohes Delta quantisieren `floor((d + grid/2)/grid)*grid`.
   - `magnetic`: Zielposition berechnen, auf Grid snappen, Delta zurückrechnen.
4. `Layout.move_pixel(...)` anwenden, `last = mouse`, `moved = true`.
5. Bei MouseReleased: wenn `moved`, `SaveSettings()`.

Grid-Settings: `edit_grid_size_px` (16), `edit_snap_mode` (`step`/`magnetic`),
`edit_snap_to_grid`, `edit_grid_show`, `edit_grid_color`.

### Alignment-Guides
N horizontale Guides als Fraction: `alignment_guide_<i>_y`, Anzahl `alignment_guide_count`.
Beim Rendern: `screen_y = win_top + frac * win_h`, Linie über volle Breite.

### Snap an REAPERs native Fenster (sehr cooles Detail)
`GetReaperTransportPosition()` / `GetReaperTCPPosition()` enumerieren REAPERs
Transport- bzw. TCP-Fenster und liefern `(x,y,w,h)`. Mit `snap_to_reaper_transport` /
`snap_to_reaper_tcp` + `snap_offset_x/y` dockt das eigene Panel an REAPERs UI an
(TCP-Variante zieht das Panel über die TCP). Fallback auf gecachte letzte Position,
falls das Fenster gerade nicht gefunden wird.
→ **Für uns relevant:** Focused-Track-Panel an TCP/MCP des fokussierten Tracks andocken.

---

## 3. Settings / State-Modell

- **Ein** flaches `default_settings`-Table (~471 Keys). Beim Start: simple Kopie nach `settings`.
- **Kein** Deep-Merge. Fehlende Keys aus geladenen Presets behalten einfach die Defaults,
  die schon in `settings` stehen (Migration „for free").
- Verschachtelte Strukturen nur wo nötig: `visual_metronome={...}`, `h_lines={}`, `v_lines={}`,
  `tabs={...}`, `transport_button_order={...}`.
- Color-Werte als ARGB-Ints (z.B. `play_active = 0x00FF00FF`).
- Sichtbarkeit: pro Element ein `show_<name>` Boolean.

### Zwei parallele Registries (wichtige Trennung)
1. `transport_components = {{id, name}, ...}` — für die **Settings-UI** (Liste + Routing).
2. `Layout.elems = {{name, showFlag, keyx, keyy, beforeDrag?}, ...}` — für **Position/Edit-Mode**.

Settings-Routing ist ein simples if/else über `selected_component` zu ~40
`Show<Component>Settings(ctx, w, h)`-Funktionen. Kein Reflection-Zauber, bewusst banal.

---

## 4. Presets

```
tk_transport_presets/<Name>.json     ein JSON pro Preset
```
- **Save:** `for k,v in pairs(settings) do preset_data[k]=v end` → `json.encode` → Datei.
  Tab-Settings werden **ausgeschlossen** (die leben separat in ExtState, s.u.).
- **Load:** JSON decode (in `pcall`, Korruptions-Guard) → flach in `settings` überschreiben.
  Danach `font_needs_update`/`fonts_need_rebuild`/`UpdateCustomImages()` triggern.
- **Liste:** `EnumerateFiles` über den Ordner, `.json` strippen.
- **Last-used:** `SetExtState("...", "last_preset", name, true)` + beim Start laden.
- **Dirty-Tracking:** `MarkTransportPresetChanged()` setzt `..._has_unsaved_changes=true`
  → UI zeigt Save-Button.

→ **Für uns:** wir haben schon JSON-Catalog/Migration (siehe `bindings_architecture`).
Preset-Pattern passt direkt: pro Layout-Preset eine JSON, „last used" merken.

---

## 5. Tabs (Layout-Sets umschalten)

Tab-Struktur minimal:
```lua
{ name="Main", id=1, transport_preset="", button_preset="" }
```
Tabs + Tab-Styling werden **in ExtState** als JSON gespeichert (nicht in den Presets),
damit sie über Preset-Wechsel hinweg stabil bleiben.

`SwitchToTab(id)`:
1. Tab-Liste + Tab-Styling zwischenspeichern.
2. `LoadPreset(tab.transport_preset)` (überschreibt Component-Settings).
3. Tab-Liste/Styling wiederherstellen (sonst vom Preset überschrieben).
4. `LoadButtonPreset(tab.button_preset)`.

→ Ein „Tab" = ein gespeichertes Layout, das per Klick reingeladen wird. Genau das,
was man für „mehrere Focused-Track-Panel-Layouts" will.

---

## 6. Buttons — data-driven & dreifach-modal

### Definition-Table + Order-Array
```lua
DEFAULT_BUTTON_ORDER = {"rewind","play","stop","pause","record","loop","forward"}
TRANSPORT_BTN_DEFS = {
  play = { id="PLAY", size_key="play", command=1007,
           use_custom="use_custom_play_image", custom_img="play",
           draw_fn="DrawPlay" },
  ...
}
```
Eine einzige Render-Schleife iteriert `settings.transport_button_order`, schlägt jede
Definition in `BTN_DEFS` nach und zeichnet. **Reordern = Array-Elemente tauschen**
(Up/Down-Arrows in den Settings vertauschen Nachbarn).

### Mode-Suffix-Pattern (der eigentliche Hebel)
```lua
local suffix = ({[0]="_text",[1]="_graphic",[2]="_custom"})[settings.transport_mode]
local function GetModeKey(base) return base..suffix end
```
Alle Appearance-Keys existieren dreifach: `transport_button_size_text/_graphic/_custom`,
`transport_spacing_*`, `transport_bg_color_*`, `play_active_*` usw.
→ Dieselbe Settings-UI und Render-Logik bedient alle drei Modi, nur der Suffix wechselt.
Drei komplette, unabhängig konfigurierbare Looks ohne Code-Duplikat.

### Drei Render-Modi
- **Text** (0): native `ImGui_Button`, Breite via `CalcTextSize`+Padding.
- **Graphic** (1): Vektor-Icons über DrawList (`AddTriangleFilled`, `AddRectFilled`,
  `AddCircleFilled`, `PathLineTo/PathStroke`, `AddNgonFilled`). `graphic_style` 0..11.
  → **Resolution-independent, keine Bitmaps.** Für uns ideal.
- **Custom** (2): PNG-Bilder statt Vektoren, sonst gleiche Positionierung.

Optionaler Button-Background pro Mode: color/hover/active, padding(x), rounding, border.

### Custom-Buttons (eigenes Submodul `custom_buttons.lua` + `button_renderer.lua`)
Vollwertige user-definierte Buttons mit reichem Schema, u.a.:
- Position (dual frac/px), `width`/`height`, `group`, `z_order`, `visible`
- `left_click = {command, name, type}` (REAPER-Action-ID, main vs MIDI-editor)
- `right_menu = {items={...}}` (Kontextmenü)
- Toggle-Modi `radio`/`toggle`/`cycle`, `show_toggle_state`, On/Off-Farben
- Bild-Support: `image_path`, `image_only`, Tinting pro State (normal/hover/pressed/on/off),
  `show_text_with_icon`, `text_position` (left/right/overlay/center), `vertical_text`
- State-basierter Text: `text_normal/hover/pressed/on/off`
- Gruppen-Sichtbarkeits-Toggle (`is_group_visibility_toggle`, `target_group`)

Gespeichert als JSON-Array in `tk_transport_buttons/current_buttons[_tabN].json`;
Presets in `tk_transport_buttons/presets/`.

Skalierung: `custom_buttons_scale_mode` (0=uniform, 1=per-group),
`custom_buttons_scale_with_width/height` (gegen `ref_width/height`),
`custom_buttons_image_scale`, `custom_buttons_auto_scale` (+ `target_width`:
schrumpft Buttons automatisch, damit sie passen).

---

## 7. Z-Order als Setting

`z_order_<element>` Integers im `settings`-Table. `ShowZOrderSettings` zeigt eine Tabelle,
nach aktuellem Wert sortiert, mit Up/Down-Arrows die Nachbarwerte tauschen + „Reset".
Beim Rendern werden Elemente nach aufsteigendem z_order gezeichnet (Custom-Buttons = 50,
also immer oben). Simpel, aber gibt dem User volle Kontrolle übers Layering.

---

## Konkrete Übertragung auf unser Focused-Track-Panel

Unser Panel ist C++/ReaImGui (extension/), nicht Lua — aber die Architektur ist 1:1 portierbar:

1. **Element-Registry einführen:** jedes Panel-Element (Trackname, Volume, Pan, FX-Slots,
   Send-Window, Buttons …) bekommt `{name, showFlag, keyx, keyy}` + optionalen Settings-Block.
2. **Dual-Koordinaten + `move_pixel`-Logik** als kleines Layout-Util portieren
   (Fraction kanonisch, Pixel während Drag, Sync zurück).
3. **Edit-Mode** mit Drag-Overlay, Grid-Snap (`step`/`magnetic`) und `StoreElementRect`
   nach jedem Element. Optional Snap an TCP/MCP des fokussierten Tracks
   (analog `GetReaperTCPPosition`) — passt perfekt zum „focused track" Konzept.
4. **Layout-Presets** als JSON (haben wir Infra für, s. `bindings_architecture` /
   Migration-System). „Last used" merken.
5. **Z-Order pro Element** als Setting + Reorder-UI.
6. Falls wir konfigurierbare Buttons im Panel wollen: **Order-Array + Defs-Table +
   Mode-Suffix**-Pattern übernehmen; Vektor-Icons via DrawList statt Bitmaps.

### Gotchas / bewusste Design-Entscheidungen von TK
- Flache Keys + Naming-Convention statt verschachtelter Objekte → triviale Persistenz,
  free Migration (fehlende Keys = Default). Wir machen es im Catalog ähnlich.
- Pixel führt beim Drag, Fraction beim Speichern — nicht verwechseln, sonst Jitter/Drift.
- Y darf bis `-100` (Element über Fensterkante) — Clamp-Range nicht zu eng wählen.
- Tabs/Tab-Styling **getrennt** von Presets speichern, sonst überschreibt ein Preset-Load
  die Tab-Leiste.
- Nach Preset-Load Fonts/Images explizit rebuilden (lazy-Resolved Ressourcen!) — deckt sich
  mit unserer `reaimgui_v010_pairing_rules`-Erfahrung (Symbol-/Ressourcen-Hygiene).

---

## Quell-Referenzen (TK_TRANSPORT.lua)
- `Layout.move_frac` ~1282, `Layout.move_pixel` ~1287
- `ScalePosX/Y` ~1386, `DrawPixelXYControls` ~1312
- `StoreElementRect(Union)` ~1409
- `Layout.elems` Registry ~1126, `transport_components` ~1429
- `ShowComponentList/Settings` ~1453/1466, `ShowSettings` ~6637
- `default_settings` ~616–1093, Init ~1095
- `SaveSettings/LoadSettings` ~5373, `SavePreset/LoadPreset/GetPresetList` ~5430–5503
- Tabs `SaveTabsToExtState/LoadTabsFromExtState/SwitchToTab/RenderTabBar` ~6139–6477
- `ShowZOrderSettings` ~4335, z_order Defaults ~1069
- `Transport_Buttons` ~8813, `DrawTransportGraphics` ~9582
- `ShowTransportButtonSettings` ~1536, `ShowTransportButtonsImagesSettings` ~4794
- `RenderEditModeOverlays` ~13241, `RenderAlignmentGuides` ~13336
- `GetReaperTransportPosition` ~1172, `GetReaperTCPPosition` ~1227
- Submodule: `custom_buttons.lua`, `button_renderer.lua`, `button_editor.lua`
