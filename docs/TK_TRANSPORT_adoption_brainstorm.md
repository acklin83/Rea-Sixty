# Brainstorming — was wir von TK_TRANSPORT übernehmen

Folgt auf `docs/TK_TRANSPORT_flexible_layout_learnings.md`.
Frage: welche TK-Elemente lohnen sich für unser **Focused-Track-Panel** und unser **Learn-HUD**?

Datum: 2026-06-15

---

## 0. Vorbedingung — die Tech-Frage zuerst klären

| Panel | Heute | Limit | TK |
|---|---|---|---|
| Focused-Panel | JS_Composite (LICE auf Host-Window) | nur titled Windows, schwärzt Arrange/TCP, nicht überall platzierbar | — |
| Learn-HUD | pures `gfx` | echtes Fenster, dock-/verschiebbar, aber Low-Level (kein Widget, manuelles Hit-Test) | — |
| TK Transport | **ReaImGui** | frei platzierbar überall, Multi-Monitor, reiche Widgets | ✅ |

→ **Spike liegt vor:** `extension/scripts/rea_sixty_focused_panel_imgui.lua` (ReaImGui, frameless,
place-anywhere, gleiche Daten/Options-Keys). **Erst entscheiden ob ReaImGui = neuer Standard**,
dann erst die folgenden Features draufsetzen — fast alles unten setzt eine ImGui-Basis voraus.

---

## 1. Focused-Track-Panel — Übernahme-Kandidaten

### A — Sofort sinnvoll (hoher Nutzen, wenig Aufwand)
1. **Place-anywhere via ReaImGui** *(Spike fertig)* — der eigentliche Schmerzpunkt. Frameless
   Window, frei draggbar, Pos/Size persistent. Löst das macOS-Black-Out-Problem komplett.
2. **Frac/Pixel-Positioning für die Inhalte** — wenn wir mehr als CS/BC reinpacken (s. F),
   jedes Element mit `<name>_x/_y` + `_px` platzierbar. TKs `move_pixel`-Sync-Trick übernehmen.
3. **Presets als JSON** + „last used" — wir haben Catalog/Migration-Infra schon
   ([[bindings_architecture]]). Mehrere Panel-Looks (kompakt / breit / nur-CS / Studio) speicherbar.
4. **Window-Alpha + Rounding + Border-Color** als Settings — TK hat `window_alpha`,
   `window_rounding`. Trivial, macht das Panel „premium". (Spike hat Alpha/Rounding/Border schon.)
5. **One-line / two-line Mode** haben wir bereits — TKs Mode-Suffix-Pattern zeigt, wie man das
   sauber auf beliebig viele Layout-Varianten erweitert.

### B — Stark, mittlerer Aufwand
6. **Edit-Mode mit Drag + Grid-Snap** (`step`/`magnetic`) + Alignment-Guides. Lohnt sich sobald
   das Panel mehrere frei platzierbare Sub-Elemente hat.
7. **Snap an REAPERs TCP/MCP** (`GetReaperTCPPosition`-Äquivalent) — **konzeptionell perfekt**
   fürs „focused track": Panel dockt automatisch an die TCP/MCP des fokussierten Tracks an.
   Das ist die elegante Lösung für „wo soll's hin" — besser als manuelles Platzieren.
8. **Z-Order pro Element** — erst relevant bei mehreren überlappenden Elementen.
9. **Tabs = Layout-Sets** — pro Tab ein gespeichertes Panel-Layout, Klick zum Wechseln.
   Z.B. „Mix"-Layout vs „Tracking"-Layout.

### C — Optional / später
10. **Vektor-Icons via DrawList** (resolution-independent) statt Text-Tags „CS/BC" — könnte
    hübsche Status-Indikatoren geben (Plug-in-aktiv-Dot, Bypass-State in Domain-Farbe).
11. **Custom-Buttons-System** (Action-binding, Toggle-States, Bilder) — wahrscheinlich
    Overkill fürs Focused-Panel; eher was für ein separates Control-Panel falls je gewünscht.

### D — NICHT übernehmen
- Battery/Local-Time/Matrix-Ticker/Waveform-Scrubber/Shuttle-Wheel — TK-Transport-spezifisch,
  für uns irrelevant.
- TKs flache 471-Key-Settings-Tabelle als Stil — wir haben strukturierten Catalog, dabei bleiben.

### F — Inhalts-Ideen, die der TK-Rahmen ermöglicht
Wenn das Panel flexibel wird, könnten wir mehr fokussierte Infos zeigen (jedes als
toggle-/platzierbares Element):
- Mehr als nur CS/BC: ganze CS-Chain-Übersicht, GR-Meter des Comp, Input-Level (haben JSFX-Probe,
  [[input_level_jsfx]]).
- Last-touched-Param mit **Live-Wert + kleinem Bar/Knob** statt nur Text.
- Track-Record/Monitor-State, Bank-/Page-Indikator des Surface.

---

## 2. Learn-HUD — Übernahme-Kandidaten

Learn-HUD ist heute pures `gfx`: zeichnet die UC1-Surface-Map als Vektor-Mockup, normalisiert
860×660 → skaliert auf jede Größe. Es ist **read-only** und schon recht flexibel in der Skalierung,
aber `gfx` ist mühsam (manuelles Hit-Test, kein echtes Theming, nur Docking).

### A — Sinnvoll
1. **Migration gfx → ReaImGui** (gemeinsam mit dem Focused-Panel-Entscheid). Vorteile:
   - DrawList kann genau dieselben Vektor-Primitives wie unser gfx-Mockup
     (`AddCircleFilled`, `AddRect`, `AddNgonFilled`, `PathStroke`) → 1:1 portierbar.
   - Echtes Window-Handling: frei platzierbar überall, nicht nur Docker.
   - Hover-Tooltips „for free" (`ImGui_IsItemHovered`) → Control-Name/Param beim Drüberfahren,
     statt alles permanent reinzuquetschen.
2. **Window-Alpha / Rounding / Border** — wie beim Focused-Panel, macht das HUD als Overlay
   dezenter (halbtransparent über dem Plug-in liegend).
3. **Presets** — verschiedene HUD-Größen/Positionen pro Workflow speichern.

### B — Stark
4. **`graphic_style`-Idee (mehrere Render-Stile)** — TK hat 12 Icon-Stile über einen
   Style-Index. Für das HUD: z.B. „Schematic" (jetzt) vs „Foto-Realistic" vs „Kompakt-Liste"
   als umschaltbare Darstellungen derselben Map. Mode-Suffix-Pattern macht das sauber.
5. **Domain-Farben als zentrale Settings** (CS/BC-RGB) — teilen wir uns schon mit dem
   MCP-Overlay; TKs Color-Settings-Struktur (mit hover/active-Varianten) zeigt, wie man das
   konsistent über alle Panels führt. → **ein gemeinsamer Color-Block** für Overlay + Focused +
   HUD (wie TK Farben global hält).
6. **Edit-Mode/Grid** ist fürs HUD weniger wichtig (Layout ist durch die Hardware-Geometrie
   vorgegeben), aber **freie Platzierung + Resize** (ImGui-Window) schon.

### C — Cross-cutting (beide Panels gemeinsam)
7. **Gemeinsames „Rea-Sixty UI"-Modul** in Lua: ein kleines shared file mit
   - Frac/Pixel-Layout-Util (`move_pixel`, `ScalePos`)
   - Preset-Save/Load (JSON-Ordner + last-used)
   - gemeinsamer Color-/Font-/Alpha-Settings-Block + ein „Style"-Apply
   - Edit-Mode-Overlay
   So bauen wir TKs Flexibilität **einmal** und nutzen sie in Focused-Panel **und** Learn-HUD
   (und ggf. Inserts-Overlay). TK macht's monolithisch in einem File — wir teilen es.

---

## 3. Vorschlag zur Reihenfolge

1. **Spike testen** (`rea_sixty_focused_panel_imgui.lua`) — fühlt sich place-anywhere richtig an?
   Look/Lesbarkeit ok? Performance der defer-Loop ok?
2. Wenn ja: **ReaImGui als Standard** für Focused-Panel beschliessen, Composite-Variante ablösen
   (oder als Fallback behalten für „eingebettet"-Fans).
3. **Shared UI-Modul** extrahieren (Punkt 2C-7): Layout-Util + Presets + Style + Edit-Mode.
4. **Learn-HUD** auf dieselbe Basis heben (gfx → ImGui), Vektor-Map 1:1 portieren, Tooltips dazu.
5. Dann inkrementell: TCP/MCP-Snap (1A-7), mehr Inhalts-Elemente (1F), Presets-UI, Tabs.

---

## Offene Fragen an Frank
- Soll das Focused-Panel **über** dem Arrange floaten (ReaImGui) oder willst du die *eingebettete*
  Composite-Variante als Option behalten?
- TCP/MCP-Auto-Snap fürs Focused-Panel — interessant, oder lieber free placement?
- Learn-HUD auf ImGui mitziehen, oder erst Focused-Panel fertig und HUD später?
