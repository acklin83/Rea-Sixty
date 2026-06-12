-- spike_inserts_overlay.lua
--
-- PROOF-OF-CONCEPT / CAPABILITY-PROBE für das geplante "grafische Overlay
-- statt FX-Rename" (Active CS/BC marker in REAPER's TCP/MCP Inserts list).
--
-- Hintergrund: Heute schreibt die Extension einen Glyph-Präfix in REAPERs
-- "renamed_name" (siehe main.cpp reconcileInsertMarkersForTrack_), was den
-- FX-Namen verändert und das Projekt "dirty" macht. Ziel ist stattdessen ein
-- nicht-destruktives Grafik-Overlay, das die vom Surface getriebene CS/BC-
-- Instanz direkt in der Mixer/TCP-Insert-Liste hervorhebt.
--
-- Der WT Graphical Annex (Inspiration) ist ein angedocktes gfx-Spiegel-Panel
-- und zeichnet NICHT auf den nativen Mixer. Der bewährte Weg, direkt auf die
-- native UI zu zeichnen, ist JS_Composite aus js_ReaScriptAPI + eine LICE-
-- Bitmap, positioniert über GetThingFromPoint-Reverse-Hit-Test.
--
-- Dieser Spike validiert genau die zwei Unbekannten, BEVOR wir die echte
-- Integration (Extension publiziert Active-State -> Companion zeichnet) bauen:
--   1) Liefert GetThingFromPoint pro FX-ZEILE in der Insert-Liste eine
--      unterscheidbare Kennung (fx_0 / fx_1 / ...), sodass wir das exakte
--      Zeilen-Rechteck per Scan bestimmen können?
--   2) Lässt sich mit JS_Composite ein Highlight passgenau auf diese Zeile
--      über den nativen Mixer legen (und sauber wieder entfernen)?
--
-- Dummy-State: markiert wird die FX-Zeile UNTER DER MAUS (statt der echten
-- aktiven Instanz). Bedienung: dieses Script starten, dann mit der Maus über
-- die FX-Insert-Liste im Mixer (oder TCP) fahren. Das kleine gfx-Fenster zeigt
-- den Live-Readout; schließen beendet den Spike.
--
-- Benötigt: js_ReaScriptAPI (für das Compositing). Ohne läuft nur der
-- Diagnose-/Scan-Teil, der Overlay-Teil wird übersprungen und gemeldet.

------------------------------------------------------------------------
-- Kleine Helfer
------------------------------------------------------------------------
local function has(fn) return reaper.APIExists(fn) end

local function ver()
  local v, vd = (reaper.GetAppVersion()):match("(%d+)%.(%d+)")
  return tonumber(v) or 0, tonumber(vd) or 0
end

-- Capability-Report einmalig in die Console.
local CAP = {
  GetThingFromPoint = has('GetThingFromPoint'),
  GetTrackFromPoint = has('GetTrackFromPoint'),
  JS_Composite      = has('JS_Composite'),
  JS_WindowFromPoint= has('JS_Window_FromPoint'),
  JS_GetClientRect  = has('JS_Window_GetClientRect'),
  JS_LICE_Create    = has('JS_LICE_CreateBitmap'),
  JS_LICE_FillRect  = has('JS_LICE_FillRect'),
  JS_Invalidate     = has('JS_Window_InvalidateRect'),
}

do
  local vM, vm = ver()
  local lines = { "=== Rea-Sixty Inserts-Overlay Spike ===",
    string.format("REAPER %d.%02d", vM, vm) }
  for k, v in pairs(CAP) do lines[#lines+1] = string.format("  %-20s %s", k, v and "OK" or "FEHLT") end
  if not CAP.JS_Composite then
    lines[#lines+1] = "!! js_ReaScriptAPI fehlt -> Overlay-Test deaktiviert (nur Scan/Diagnose)."
  end
  reaper.ShowConsoleMsg(table.concat(lines, "\n") .. "\n\n")
end

local CAN_OVERLAY = CAP.JS_Composite and CAP.JS_WindowFromPoint
  and CAP.JS_GetClientRect and CAP.JS_LICE_Create and CAP.JS_LICE_FillRect

------------------------------------------------------------------------
-- Reverse-Hit-Test: das Rechteck der FX-Zeile unter (mx,my) bestimmen.
--
-- Idee: GetThingFromPoint liefert pro Punkt (window, segment, details).
-- Solange Fenster+Details konstant bleiben, sind wir in derselben Zeile.
-- Wir scannen von (mx,my) nach oben/unten bzw. links/rechts bis sich die
-- Kennung ändert -> Zeilen-Rechteck in SCREEN-Koordinaten.
--
-- Gibt nil zurück, wenn der Punkt keine unterscheidbare FX-Kennung trägt
-- (dann ist GetThingFromPoint für FX-Zeilen zu grob -> wichtige Erkenntnis).
------------------------------------------------------------------------
local SCAN_MAX = 400  -- Sicherheitsgrenze in px pro Richtung

local function thing(x, y)
  -- normalisiert die Rückgabe auf "window|details" für den Vergleich
  local win, seg, det = reaper.GetThingFromPoint(x, y)
  return win or "", seg or "", det or ""
end

local function isFxDetail(det)
  -- REAPER-Detail-Strings für die Insert-Liste enthalten "fx" (z.B. "fx_0").
  return det ~= "" and det:lower():find("fx", 1, true) ~= nil
end

local function findRowRect(mx, my)
  if not CAP.GetThingFromPoint then return nil end
  local win0, seg0, det0 = thing(mx, my)
  if win0 == "" then return nil end
  -- Schlüssel der aktuellen Zeile: bei FX-Zeilen die Detail-Kennung, sonst
  -- segment+details (so sehen wir auch, wenn alles als ein Block kommt).
  local key0 = win0 .. "|" .. seg0 .. "|" .. det0

  local function same(x, y)
    local w, s, d = thing(x, y)
    return (w .. "|" .. s .. "|" .. d) == key0
  end

  local top, bot, left, right = my, my, mx, mx
  for d = 1, SCAN_MAX do if same(mx, my - d) then top = my - d else break end end
  for d = 1, SCAN_MAX do if same(mx, my + d) then bot = my + d else break end end
  local cy = math.floor((top + bot) / 2)
  for d = 1, SCAN_MAX do if same(mx - d, cy) then left = mx - d else break end end
  for d = 1, SCAN_MAX do if same(mx + d, cy) then right = mx + d else break end end

  return { l = left, t = top, r = right + 1, b = bot + 1,
           win = win0, seg = seg0, det = det0, isFx = isFxDetail(det0) }
end

------------------------------------------------------------------------
-- Compositing: Highlight-Rechteck auf das native Fenster unter der Maus.
------------------------------------------------------------------------
local g_bitmap, g_lastHwnd
local function clearOverlay()
  if g_lastHwnd and CAP.JS_Composite then
    reaper.JS_Composite(g_lastHwnd, 0, 0, 0, 0, g_bitmap or 0, 0, 0, 0, 0, false)
    if CAP.JS_Invalidate then reaper.JS_Window_InvalidateRect(g_lastHwnd, 0, 0, 99999, 99999, false) end
  end
  g_lastHwnd = nil
end

-- rect in SCREEN-Koordinaten -> Highlight auf hwnd unter der Maus legen.
local function drawOverlay(rect)
  if not CAN_OVERLAY then return false end
  local hwnd = reaper.JS_Window_FromPoint(rect.l + 2, rect.t + 2)
  if not hwnd then return false end
  local ok, cl, ct = reaper.JS_Window_GetClientRect(hwnd)
  if not ok then return false end

  local w = math.max(1, rect.r - rect.l)
  local h = math.max(1, rect.b - rect.t)
  if not g_bitmap then g_bitmap = reaper.JS_LICE_CreateBitmap(true, w, h) end
  reaper.JS_LICE_Resize(g_bitmap, w, h)
  reaper.JS_LICE_Clear(g_bitmap, 0)                       -- voll transparent
  -- halbtransparente Füllung + heller Rahmen = "aktive Instanz"-Look
  reaper.JS_LICE_FillRect(g_bitmap, 0, 0, w, h, 0x00C8FF, 0.22, "COPY")
  reaper.JS_LICE_DrawRect(g_bitmap, 0, 0, w - 1, h - 1, 0x00C8FF, 0.85, "COPY")

  -- screen -> client des Ziel-hwnd
  local dstx = rect.l - cl
  local dsty = rect.t - ct
  if g_lastHwnd and g_lastHwnd ~= hwnd then clearOverlay() end
  reaper.JS_Composite(hwnd, dstx, dsty, w, h, g_bitmap, 0, 0, w, h, false)
  if CAP.JS_Invalidate then reaper.JS_Window_InvalidateRect(hwnd, dstx, dsty, dstx + w, dsty + h, false) end
  g_lastHwnd = hwnd
  return true
end

------------------------------------------------------------------------
-- gfx-Fenster: Lifecycle + Live-Readout
------------------------------------------------------------------------
gfx.init("Rea-Sixty Inserts-Overlay Spike", 560, 230, 0, 200, 200)
gfx.setfont(1, "Arial", 15)

local last = { key = "", rect = nil, ok = false }

local function fmtRect(r)
  if not r then return "—" end
  return string.format("x=%d y=%d  w=%d h=%d", r.l, r.t, r.r - r.l, r.b - r.t)
end

local function draw()
  gfx.set(0.10, 0.10, 0.12, 1); gfx.rect(0, 0, gfx.w, gfx.h, true)
  gfx.set(0.85, 0.87, 0.90, 1); gfx.setfont(1)
  local r = last.rect
  local Y = 12
  local function ln(s) gfx.x, gfx.y = 14, Y; gfx.drawstr(s); Y = Y + 22 end
  ln("Maus über die FX-Insert-Liste im Mixer/TCP bewegen.")
  ln(CAN_OVERLAY and "Overlay: AKTIV (JS_Composite)" or "Overlay: AUS (js_ReaScriptAPI fehlt) — nur Diagnose")
  ln("")
  ln("GetThingFromPoint:")
  if r then
    ln(string.format("   win=%s  seg=%s  det=%s", r.win, r.seg, r.det))
    ln(string.format("   Zeilen-Rect: %s   FX-Zeile erkannt: %s",
        fmtRect(r), r.isFx and "JA" or "nein"))
  else
    ln("   (kein Element / keine Kennung unter der Maus)")
  end
  ln("")
  ln("Fenster schließen beendet den Spike.")
end

------------------------------------------------------------------------
-- Defer-Loop
------------------------------------------------------------------------
local function loop()
  local mx, my = reaper.GetMousePosition()
  local rect = findRowRect(mx, my)

  if rect then
    local key = rect.win .. "|" .. rect.seg .. "|" .. rect.det
    if key ~= last.key then
      -- nur bei Änderung loggen (Console nicht fluten)
      reaper.ShowConsoleMsg(string.format(
        "[hit] win=%-16s seg=%-10s det=%-10s rect=%s isFx=%s\n",
        rect.win, rect.seg, rect.det, fmtRect(rect), tostring(rect.isFx)))
      last.key = key
    end
    last.rect = rect
    -- Dummy-State: Zeile unter der Maus = "aktive Instanz"
    if rect.isFx then last.ok = drawOverlay(rect) else clearOverlay() end
  else
    last.rect = nil; last.key = ""
    clearOverlay()
  end

  draw()
  gfx.update()
  if gfx.getchar() >= 0 then reaper.defer(loop) end
end

local function shutdown()
  clearOverlay()
  if g_bitmap and CAP.JS_LICE_Create then reaper.JS_LICE_DestroyBitmap(g_bitmap) end
  gfx.quit()
  reaper.ShowConsoleMsg("\n=== Spike beendet ===\n")
end

reaper.atexit(shutdown)
loop()
