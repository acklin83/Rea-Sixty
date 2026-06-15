# Plan — Learn-HUD: Mockup-View (CS/BC separat) mit List/Mockup-Toggle

Kontext: Der ImGui-Learn-HUD (`rea_sixty_assignment_hud_imgui.lua`) zeigt aktuell die
**gruppierte Liste**. Mit der ImGui-Schärfe wird das ursprüngliche **physische
Mockup** wieder praktikabel (im gfx-HUD wurde es seinerzeit durch die Liste ersetzt,
weil gfx Platz verschwendete + Namen abschnitt). Ziel: umschaltbar List ↔ Mockup,
pro Domain (CS / BC getrennt, über die bestehenden Tabs).

Datum: 2026-06-15

---

## Schlüssel-Befund: Daten sind schon da

`hudGeometryUc1_()` (SettingsScreen.cpp:7526) published pro Control:
```
idx;shape;cx;cy;r;w;h;dom;label;section
  shape 0=knob 1=toggle 2=dynbtn ; dom 'c'=CS 'b'=BC
  cx,cy,r,w,h = Pixel im 860×660-Raum (UC1-Face-Layout aus kUc1Controls)
```
Der HUD parst das bereits vollständig (`parseGeom` → `geom.ctrl[idx]` mit
shape/cx/cy/r/w/h/dom/label/sec). Der Listen-Modus nutzt cx/cy/r/w/h nicht — der
Mockup nutzt genau die. **→ Keine Extension-/C++-Änderung. Reiner Lua-Job.**

Bonus: `handleControlClick` kann **shape 0 (Kreis-Hittest)** schon — Click-to-Learn
funktioniert im Mockup ohne Zusatzarbeit, solange wir die ctrlRects mit shape+coords
füllen wie im Listen-Modus.

---

## Geometrie-Realität (aus kUc1Controls)

- **CS** belegt die volle Breite: linke Spalte (Filter/EQ, x≈82–204), Centre (IN/OUT
  Gain, x≈310–550), rechte Spalte (Comp/Gate/Channel, x≈632–793). y 24–600.
- **BC** ist kompakt im Centre: x≈370–490, y≈172–376 (7 Knobs + IN-Toggle).

→ Pro Domain unterschiedliche Bounding-Box. **GEMEINSAMER Maßstab (Frank 2026-06-15):**
EIN scale aus der vollen UC1-Face (860×660) berechnen und auf BEIDE Domains anwenden —
damit ein Knob in der CS- wie in der BC-Ansicht exakt gleich groß ist (kein „BC zu
groß"). Jede Domain wird mit diesem scale in ihrer Face-Position gezeichnet und in der
Fensterfläche zentriert. CS füllt damit ~die ganze Breite, BC nutzt nur den Centre —
aber Control-Größe ist konsistent. Das ist „CS und BC separat, gleicher Maßstab".

---

## Umsetzung

### 1. View-Toggle
- Neuer ExtState-Key `hud_imgui_view` ("list" default | "mockup").
- Rechtsklick-Menü: Submenü **View** → „List" / „Mockup" (mit Checkmark), oder ein
  einzelner Toggle-Eintrag „Mockup view".
- `render()` verzweigt im `present`-Zweig:
  `view=="mockup" and renderMockup(st,asn) or renderList(st,asn)`.

### 2. `renderMockup(st, asn)`
```
domChar = activeTab=="cs" and "c" or "b"
rgb     = domain colour (csRgb/bcRgb), bzw. weiss wenn hud_text_white
-- 0) GEMEINSAMER scale aus der vollen Face (einmal, für beide Domains gleich):
--      faceW, faceH = geom.w, geom.h   (860×660)
--      area = (WW-2M) × (WH-TAB_H-paramRow-2M)   -- Platz unter Tab-Strip
--      scale = min(area.w/faceW, area.h/faceH)    -- aspect-preserving, SHARED
-- 1) bbox NUR der aktiven Domain (zum Zentrieren der Domain in der Fläche):
--      cxMid = (bbMinX+bbMaxX)/2 ; cyMid = (bbMinY+bbMaxY)/2  (Face-Space)
--      ox = WW/2 - cxMid*scale ; oy = (TAB_H + area.h/2) - cyMid*scale
-- 2) je Control:
--      sx = ox + cx*scale ; sy = oy + cy*scale ; rr = r*scale
--      mapped = asn[idx] ~= nil ; a = asn[idx]
--      Knob (shape 0):  AddCircleFilled(bg) + AddCircle(ring, domainfarbe wenn mapped)
--      Toggle/DynBtn:   AddRectFilled + AddRect(border)   (w,h * scale)
--      Zustandsfarben wie in der Liste:
--        mapped → domain colour voll ; present-unmapped → grau ; !present → ghost
--      PARAM-NAME fix unter dem Control (a.name, "—" wenn unmapped), zentriert,
--        fit() auf Zellbreite, kleiner Font; "inv" angehängt wenn a.inv
--      Learn-Puls: idx==learnIdx → pulsierender Ring (frame-Puls, wie drawRow)
-- 3) ctrlRects[#+1] = {idx, shape=c.shape, x,y(,r) / w,h} in LOCAL coords
--    → Click-to-Learn + Hover greifen automatisch
```

### 3. Param-Name fix + Slot-Name als Tooltip  (Frank 2026-06-15)
**VERIFIZIERT in `hudGeometryUc1_()` (SettingsScreen.cpp:7529-7553):** das published
Label-Feld ist der **kanonische SSL-360-Link-Slot-Name** („HF Gain", „Comp Ratio",
„Gate Range") — NICHT die interne terse Abkürzung. Der Code-Kommentar: die `.label`-
Abkürzungen „don't appear anywhere on the real surface", werden aus der canonical
topology durch `sl->name` ersetzt (Fallback auf die Abkürzung nur, wenn ein Slot fehlt).
→ Der HUD hat in `c.label` also bereits „HF Gain" o.ä., kein „HFGN".

**Lehre aus dem user-defined-names-Problem heute:** der angezeigte Wert muss der
**gebundene Param-Name** sein (`asn[idx].name`, aus `hudDisplayName_` = Custom-Label
sonst VST3-Param-Name) — der respektiert User-Custom-Labels, genau wie die Liste.
- **Fix** unter jedem Control: `asn[idx].name`, zentriert, `fit()` auf Zellbreite,
  „inv" angehängt. Unmapped → dim „—".
- **Tooltip** (Maus im Control-Rect, `ImGui_SetTooltip`): der Slot-Name (`c.label`,
  z.B. „HF Gain") = „welches physische Control" + der volle Param-Name (falls `fit()`
  gekürzt hat).
- Param-Name braucht fixe Höhe → kleine `paramRow` pro Control in die scale-Fläche
  einrechnen (Schritt 0), damit Texte nicht ins nächste Control laufen.
- OFFEN: bei unmapped Controls zeigt „fix" nur „—" → Slot-Name nur im Tooltip. Ggf.
  für unmapped den Slot-Namen fix zeigen, damit man das Control erkennt. (Frank?)

### 4. Hittest
`handleControlClick` bleibt wie es ist (kann Kreis + Rect). Nur `renderMockup` muss die
ctrlRects mit den Mockup-Coords + `shape` füllen (Knob shape 0 trägt zusätzlich `r`).

### 5. Mock-Publisher updaten (für Test ohne Hardware)
`rea_sixty_hud_mock.lua` nutzt aktuell Dummy-Coords (100,100). Für den Mockup-Test:
echte cx/cy/r/w/h aus kUc1Controls für die Mock-Controls eintragen (CS-Subset +
BC-Subset), sonst überlappen im Mockup alle auf einem Punkt.

---

## Entscheidungen (mit Frank geklärt 2026-06-15)

1. ✅ Per-Domain getrennt (über die Tabs), **aber gemeinsamer Maßstab** — Controls in
   CS und BC exakt gleich groß. Nicht: Bounding-Box-pro-Domain (BC würde zu groß).
2. ✅ **Param-Name fix** anzeigen (`asn[idx].name`, wie die Liste, respektiert
   Custom-Labels), **kanonischer Slot-Name (`c.label`, z.B. „HF Gain") als Tooltip**.
   Nicht umgekehrt. (Das published Label IST der Slot-Name, kein terses „HFGN" —
   verifiziert in hudGeometryUc1_.)
3. ✅ Alle Controls zeigen, ungemappte gedimmt („—") — auf einen Blick sehen was belegt
   ist.

## Aufwand / Risiko
- ~1 neue Render-Funktion (~80–120 Zeilen) + Menü-Toggle + Mock-Coords.
- Risiko niedrig: Geometrie + Hittest-Infra existieren, keine C++-Änderung.
- Halber Tag inkl. Feinschliff (Skalierung, Label-Fit, Tooltip).

## Reihenfolge
1. Mock mit echten Coords (damit testbar).
2. View-Toggle + `renderMockup` (Grundgerüst: Kreise/Rects + Zustandsfarben + Skalierung).
3. Labels + Learn-Puls + Hover-Tooltip.
4. Feinschliff Skalierung/Zentrierung, ggf. Param-unter-Knob bei großem scale.
