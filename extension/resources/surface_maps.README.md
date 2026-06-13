# Surface coordinate maps (`surface_maps.json`)

Shared source of truth that maps every physical control on the UF8 / UC1 / UF1
to a position on that device's mockup PNG. **One map, two renderers:**

- **On-screen HUD** — gfx/Lua companion (`rea_sixty_inserts_overlay.lua` and its
  successor view-shell). Draws the PNG via `gfx.loadimg`/`gfx.blit` and paints
  live state (V-pot rings, fader caps, LED dots, scribble text) on top.
- **Click-to-assign mockup editor** — ReaImGui in the extension Settings.
  `ImGui_Image` + `ImGui_DrawList_*` overlay, `ImGui_InvisibleButton` over each
  control region for hit-testing. (Needs the image API added to the vendored
  ReaImGui header — see the on-screen-display-architecture memory.)

## Conventions

- **Coordinates are normalised `0..1`**, origin top-left, relative to the device
  PNG. The renderer multiplies by the rectangle it actually drew the image into,
  so the same numbers work at any window size / Retina scale.
- **Control ids match `uf8::bindings::toName()`** (`Bindings.cpp` `kNames`) for
  global buttons, so the map cross-references the binding system directly
  (highlight a control → show/edit its current binding).
- **Per-strip controls** use the `strips` template instead of 8×N explicit
  cells: a control's `x = strips.x0 + (n-1)*strips.pitch + control.dx`, with
  `y` / size from the per-control row. Strip index `n` is 1..`count`. Per-strip
  ids the renderer/editor synthesise: `fader_N`, `vpot_N`, `vpot_push_N`,
  `select_N`, `solo_N`, `mute_N`, `rec_N`, `scribble_N`, `top_soft_N`.
- **`shape`** tells the renderer how to draw the live overlay + how the editor
  hit-tests: `ring` (encoder — arc/dot), `vfader` (vertical fader cap),
  `button` / `led` (rect or dot), `lcd` (text region).
- **`geom: null`** = not calibrated yet. Fill against the artwork:
  - `ring`   → `{ "cx":…, "cy":…, "r":… }`
  - `vfader` → `{ "x":…, "y":…, "w":…, "h":… }`  (travel area; cap y = top + (1-v)*h)
  - `button`/`led`/`lcd` → `{ "x":…, "y":…, "w":…, "h":… }`
  For the strip template, calibrate `x0` + `pitch` once, then each control row's
  `dx` (offset within a strip) + `y` + size.

## Calibration workflow (when the PNGs land)

1. Drop `uf8.png` / `uc1.png` / `uf1.png` in `resources/` (these get baked into
   the dylib + self-installed alongside the Lua, same mechanism as the
   companion script).
2. Measure control centres/rects on the image in pixels, divide by image
   width/height → normalised values. (A throwaway calibration view that prints
   `mouse_x/gfx.w, mouse_y/gfx.h` on click makes this a 5-minute job per device.)
3. Fill `geom` / the strip template. No renderer code changes — both the HUD and
   the editor pick up the new numbers from the JSON.

## Shipping

Built once per device; baked into `reaper_rea-sixty` and self-installed to
`<ResourcePath>/Scripts/rea-sixty/` (JSON) + an images dir (PNGs) on load, the
same idempotent byte-compare path as the companion Lua. Both the Lua HUD and the
C++ editor read this single file — never duplicate the geometry.
