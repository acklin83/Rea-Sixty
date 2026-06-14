-- @description Rea-Sixty — Assignment HUD (companion)
-- @author Störsender
-- @version 0.1.0
-- @provides [main] .
-- @about
--   Dockable on-screen HUD that draws the **focused plug-in's UC1 surface map**
--   as a vector mockup: every control in its physical place, mapped controls
--   ringed in their domain colour (CS / BC) with the bound parameter's name,
--   unmapped controls dimmed. Read-only in this milestone — it mirrors what the
--   hardware does so you can see a plug-in's assignments at a glance without
--   opening Settings.
--
--   Pure gfx (no ReaImGui, no JS_Composite). The extension publishes:
--     • "rea_sixty"/"hud_geom_uc1" — static control geometry (pixels in an
--       860×660 space; this script normalises by W/H so it scales to any size).
--     • "rea_sixty"/"hud_state"    — "UC1;<csPresent>;<bcPresent>;<csShort>;<bcShort>"
--     • "rea_sixty"/"hud_assign"   — one line per mapped control "<idx>;<name>;<inv>"
--   Run once to start (background defer); run again (or untick the Settings
--   checkbox) to stop. Dock state persists. Requires the Rea-Sixty extension
--   with "Assignment HUD" enabled.

local _, _, sectionID, cmdID = reaper.get_action_context()

local SECT   = "rea_sixty"
local RUNKEY = "hud_running"

if reaper.GetExtState(SECT, RUNKEY) == "1" then
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  return
end
reaper.SetExtState(SECT, RUNKEY, "1", false)

local function setToggle(on)
  if sectionID and cmdID and sectionID >= 0 then
    reaper.SetToggleCommandState(sectionID, cmdID, on and 1 or 0)
    reaper.RefreshToolbar2(sectionID, cmdID)
  end
end

------------------------------------------------------------------------
-- Tunables (live via ExtState — shared with the inserts overlay so CS/BC
-- colours stay consistent across the whole product).
------------------------------------------------------------------------
local function num(key, def)
  local v = tonumber(reaper.GetExtState(SECT, key))
  if v == nil then return def end
  return v
end
local function csRgb() return math.floor(num("overlay_cs_col", 0x33C0FF)) & 0xFFFFFF end
local function bcRgb() return math.floor(num("overlay_bc_col", 0xFFB000)) & 0xFFFFFF end

local function gset(rgb, a)
  gfx.set(((rgb >> 16) & 0xFF) / 255, ((rgb >> 8) & 0xFF) / 255,
          (rgb & 0xFF) / 255, a or 1)
end

------------------------------------------------------------------------
-- Geometry: parse "hud_geom_uc1" once (cache by raw string).
--   header: "UC1;<W>;<H>"
--   control: "<idx>;<shape>;<cx>;<cy>;<r>;<w>;<h>;<dom>;<label>"
--            shape 0=knob 1=toggle 2=dynbtn ; dom 'c'=CS 'b'=BC
------------------------------------------------------------------------
local geomRaw = nil
local geom    = { w = 860, h = 660, ctrl = {} }

local function parseGeom(raw)
  local g = { w = 860, h = 660, ctrl = {} }
  local first = true
  for line in raw:gmatch("[^\n]+") do
    if first then
      local dev, w, h = line:match("^(%a+);(%d+);(%d+)")
      if w then g.w = tonumber(w); g.h = tonumber(h) end
      first = false
    else
      local idx, shape, cx, cy, r, w, h, dom, label =
        line:match("^(%d+);(%d+);([%d%.]+);([%d%.]+);([%d%.]+);([%d%.]+);([%d%.]+);(%a);(.*)$")
      if idx then
        g.ctrl[tonumber(idx)] = {
          shape = tonumber(shape), cx = tonumber(cx), cy = tonumber(cy),
          r = tonumber(r), w = tonumber(w), h = tonumber(h),
          dom = dom, label = label or "",
        }
      end
    end
  end
  return g
end

local function refreshGeom()
  local raw = reaper.GetExtState(SECT, "hud_geom_uc1")
  if raw ~= "" and raw ~= geomRaw then
    geomRaw = raw
    geom = parseGeom(raw)
  end
end

------------------------------------------------------------------------
-- State + assignments.
------------------------------------------------------------------------
local function readState()
  local raw = reaper.GetExtState(SECT, "hud_state")
  -- "UC1;<csPresent>;<bcPresent>;<focusDom c/b/n>;<csShort>;<bcShort>"
  local dev, cs, bc, fdom, csN, bcN =
    raw:match("^(%a+);(%d);(%d);(%a);([^;]*);(.*)$")
  return {
    dev = dev or "UC1",
    csPresent = (cs == "1"),
    bcPresent = (bc == "1"),
    focusDom = fdom or "n",
    csShort = csN or "",
    bcShort = bcN or "",
  }
end

-- Active tab ("cs"/"bc") + the last focus domain we auto-switched on, so a
-- focus change follows but a manual tab click sticks until the next change.
local activeTab    = "cs"
local lastFocusDom = "n"
local tabRects     = {}   -- {dom,x,y,w,h} filled by drawTabs for hit-testing

local function readAssign()
  local raw = reaper.GetExtState(SECT, "hud_assign")
  local map = {}
  for line in raw:gmatch("[^\n]+") do
    local idx, name, inv = line:match("^(%d+);([^;]*);(%d)$")
    if idx then
      map[tonumber(idx)] = { name = name or "", inv = (inv == "1") }
    end
  end
  return map
end

------------------------------------------------------------------------
-- Window lifecycle (mirrors the inserts-overlay dock panel).
------------------------------------------------------------------------
local winOpen   = false
local rcPrev    = 0     -- previous right-button state (context-menu edge detect)
local lcPrev    = 0     -- previous left-button state (tab-click edge detect)
-- Default close to the native 860×660 aspect (+ header + footer) so controls
-- aren't crushed; fonts scale with the draw size, and the user can resize.
local DEF_W, DEF_H = 820, 690

local function winWanted() return reaper.GetExtState(SECT, "hud_on") == "1" end

local function winClose(persistOff)
  if winOpen then
    local dock = gfx.dock(-1)
    reaper.SetExtState(SECT, "hud_dock", tostring(dock or 0), true)
    gfx.quit()
    winOpen = false
  end
  if persistOff then reaper.SetExtState(SECT, "hud_on", "0", true) end
end

local function contextMenu()
  local docked = (gfx.dock(-1) & 1) == 1
  gfx.x, gfx.y = gfx.mouse_x, gfx.mouse_y
  local menu = (docked and "!" or "") .. "Dock window in Docker|Close HUD"
  local sel = gfx.showmenu(menu)
  if sel == 1 then
    if docked then gfx.dock(0) else gfx.dock(1) end
    reaper.SetExtState(SECT, "hud_dock", tostring(gfx.dock(-1) or 0), true)
  elseif sel == 2 then
    winClose(true)
  end
end

------------------------------------------------------------------------
-- Render.
------------------------------------------------------------------------
local TAB_H = 30

-- Truncate a string to fit `maxw` px (current font), adding an ellipsis.
local function fit(s, maxw)
  if s == "" then return s end
  local w = gfx.measurestr(s)
  if w <= maxw then return s end
  while #s > 1 do
    s = s:sub(1, #s - 1)
    if gfx.measurestr(s .. "\xE2\x80\xA6") <= maxw then return s .. "\xE2\x80\xA6" end
  end
  return s
end

-- Tab strip: one tab per domain, active one highlighted in its colour. Stores
-- hit-rects in tabRects for click handling in the main loop.
local function drawTabs(st)
  gfx.set(0.09, 0.09, 0.10, 1); gfx.rect(0, 0, gfx.w, TAB_H, 1)
  tabRects = {}
  local function tab(dom, present, short, x)
    local name = (dom == "cs") and "CS" or "BC"
    local label = name .. ((present and short ~= "") and ("  " .. short) or "")
    gfx.setfont(1, "Arial", 16)
    local w = gfx.measurestr(label) + 24
    local active = (activeTab == dom)
    local rgb = (dom == "cs") and csRgb() or bcRgb()
    if active then gset(rgb, present and 0.30 or 0.16)
    else           gfx.set(0.14, 0.14, 0.16, 1) end
    gfx.rect(x, 4, w, TAB_H - 6, 1)
    if active then gset(rgb, present and 1 or 0.5); gfx.rect(x, 4, w, 3, 1) end
    gset(rgb, present and 1 or 0.40)
    gfx.x, gfx.y = x + 12, 4 + math.floor((TAB_H - 6 - gfx.texth) / 2)
    gfx.drawstr(label)
    tabRects[#tabRects + 1] = { dom = dom, x = x, y = 4, w = w, h = TAB_H - 6 }
    return x + w + 4
  end
  local x = 8
  x = tab("cs", st.csPresent, st.csShort, x)
  x = tab("bc", st.bcPresent, st.bcShort, x)
end

-- Click a tab (called from the loop on a left-button edge).
local function handleTabClick(mx, my)
  for _, t in ipairs(tabRects) do
    if mx >= t.x and mx <= t.x + t.w and my >= t.y and my <= t.y + t.h then
      activeTab = t.dom
      return
    end
  end
end

local function drawControl(c, ox, oy, sc, silkFont, paramFont, st, asn)
  local x  = ox + c.cx * sc
  local y  = oy + c.cy * sc
  local isBc = (c.dom == "b")
  local present = isBc and st.bcPresent or st.csPresent
  local a = asn[c.idx]
  local mapped = (a ~= nil)
  local rgb = isBc and bcRgb() or csRgb()

  -- Ring colour conveys state; the base shape stays clearly visible in ALL
  -- states (mapped / present-unmapped / absent) so the device always reads as
  -- a device even with nothing focused. Thick ring for contrast.
  local col, alpha
  if mapped       then col, alpha = rgb,      1.0
  elseif present  then col, alpha = 0xC6CDD6, 0.95
  else                 col, alpha = 0x808892, 0.55 end
  local fillRgb = present and 0x3C4654 or 0x2C333D

  local lx, ly = x, y     -- label centre
  if c.shape == 0 then
    -- knob — filled cap + a ring 2 px thick.
    local r = math.max(5, c.r * sc)
    gset(fillRgb, 1); gfx.circle(x, y, r, 1, 1)
    gset(col, alpha)
    gfx.circle(x, y, r, 0, 1); gfx.circle(x, y, r - 1, 0, 1)
    if mapped then gfx.circle(x, y, r + 1, 0, 1) end
  else
    -- toggle / dyn button — filled box + a 2 px border.
    local w = math.max(10, c.w * sc)
    local h = math.max(10, c.h * sc)
    gset(fillRgb, 1); gfx.rect(x, y, w, h, 1)
    gset(col, alpha)
    gfx.rect(x, y, w, h, 0)
    if w > 2 and h > 2 then gfx.rect(x + 1, y + 1, w - 2, h - 2, 0) end
    lx, ly = x + w / 2, y + h / 2     -- centre for the label
  end

  -- Silk label (4-char) centred on the control.
  if c.label ~= "" then
    gfx.setfont(1, "Arial", silkFont)
    gset(0xE2E6EC, present and 1.0 or 0.7)
    local lw = gfx.measurestr(c.label)
    gfx.x, gfx.y = lx - lw / 2, ly - gfx.texth / 2
    gfx.drawstr(c.label)
  end

  -- Param name under mapped controls, in the domain colour.
  if mapped and a.name ~= "" then
    gfx.setfont(1, "Arial", paramFont)
    local r = (c.shape == 0) and (c.r * sc) or (c.h * sc / 2)
    local txt = fit(a.name, 120)
    if a.inv then txt = txt .. " inv" end
    local tw = gfx.measurestr(txt)
    gset(rgb, 1)
    gfx.x, gfx.y = lx - tw / 2, ly + r + 2
    gfx.drawstr(txt)
  end
end

local function render()
  -- Background.
  gfx.set(0.12, 0.12, 0.13, 1); gfx.rect(0, 0, gfx.w, gfx.h, 1)

  local st  = readState()
  local asn = readAssign()

  -- Auto-follow focus: when the surface's focused domain changes, switch the
  -- active tab to it. A manual tab click sticks until the next focus change.
  if st.focusDom == "c" or st.focusDom == "b" then
    if st.focusDom ~= lastFocusDom then
      activeTab = (st.focusDom == "c") and "cs" or "bc"
      lastFocusDom = st.focusDom
    end
  else
    lastFocusDom = "n"
  end

  drawTabs(st)

  -- Diagnostic footer (dim).
  local nCtrl = 0; for _ in pairs(geom.ctrl) do nCtrl = nCtrl + 1 end
  local nAsn  = 0; for _ in pairs(asn)       do nAsn  = nAsn  + 1 end
  local FOOTER_H = 16
  gfx.setfont(1, "Arial", 11)
  gset(0x707880, 0.75)
  gfx.x, gfx.y = 6, gfx.h - gfx.texth - 3
  gfx.drawstr(string.format("geom:%d  cs:%s  bc:%s  mapped:%d  tab:%s",
    nCtrl, st.csPresent and "on" or "off", st.bcPresent and "on" or "off",
    nAsn, activeTab))

  if not geom or nCtrl == 0 then
    gfx.setfont(1, "Arial", 15)
    gset(0x808890, 0.9)
    gfx.x, gfx.y = 10, TAB_H + 12
    gfx.drawstr("Waiting for surface geometry\xE2\x80\xA6")
    return
  end

  -- Collect the active domain's controls + their bounding box.
  local domChar = (activeTab == "cs") and "c" or "b"
  local list = {}
  local minx, miny, maxx, maxy = 1e9, 1e9, -1e9, -1e9
  for idx, c in pairs(geom.ctrl) do
    if c.dom == domChar then
      c.idx = idx
      list[#list + 1] = c
      local x0, y0, x1, y1
      if c.shape == 0 then
        x0 = c.cx - c.r; y0 = c.cy - c.r; x1 = c.cx + c.r; y1 = c.cy + c.r
      else
        x0 = c.cx; y0 = c.cy; x1 = c.cx + c.w; y1 = c.cy + c.h
      end
      if x0 < minx then minx = x0 end
      if y0 < miny then miny = y0 end
      if x1 > maxx then maxx = x1 end
      if y1 > maxy then maxy = y1 end
    end
  end
  if #list == 0 then return end

  local present = (activeTab == "cs") and st.csPresent or st.bcPresent

  -- SHARED scale for BOTH tabs = fit the full 860×660 device into the window.
  -- A knob is therefore the same physical size on CS and BC (like the real
  -- hardware) — no 4× jump when switching tabs. The active domain is just
  -- translated so its bbox centre sits at the window centre; the smaller
  -- domain (BC) shows centred with margins rather than being blown up.
  local m = 10
  local availW = gfx.w - 2 * m
  local availH = gfx.h - TAB_H - FOOTER_H - 2 * m
  if availW < 20 or availH < 20 then return end
  local sc = math.min(availW / geom.w, availH / geom.h)
  local cx = (minx + maxx) / 2
  local cy = (miny + maxy) / 2
  local ox = m + availW / 2 - cx * sc
  local oy = TAB_H + m + availH / 2 - cy * sc

  local silkFont  = math.max(9,  math.floor(sc * 16 + 0.5))
  local paramFont = math.max(10, math.floor(sc * 17 + 0.5))

  for _, c in ipairs(list) do
    drawControl(c, ox, oy, sc, silkFont, paramFont, st, asn)
  end

  -- "No plug-in" hint when the active domain isn't on the focused track.
  if not present then
    gfx.setfont(1, "Arial", 14)
    gset(0x9097A0, 0.85)
    local msg = (activeTab == "cs" and "No Channel-Strip" or "No Bus-Comp")
              .. " plug-in on the focused track"
    local tw = gfx.measurestr(msg)
    gfx.x, gfx.y = (gfx.w - tw) / 2, TAB_H + 6
    gfx.drawstr(msg)
  end
end

------------------------------------------------------------------------
-- Main defer loop.
------------------------------------------------------------------------
local shutdown

local function loop()
  if reaper.GetExtState(SECT, RUNKEY) ~= "1" then return shutdown() end

  local want = winWanted()
  if want and not winOpen then
    local dock = math.floor(num("hud_dock", 0))
    gfx.ext_retina = 1
    gfx.init("Rea-Sixty Assignment HUD", DEF_W, DEF_H, dock, 200, 200)
    winOpen = true
  elseif not want and winOpen then
    winClose(false)
  end

  if winOpen then
    if gfx.getchar() < 0 then return winClose(true) end   -- user closed window
    local rc = gfx.mouse_cap & 2
    if rc == 2 and rcPrev ~= 2 then contextMenu() end
    rcPrev = rc
    if winOpen then
      -- Left-click edge → tab switch (uses last frame's tab rects).
      local lc = gfx.mouse_cap & 1
      if lc == 1 and lcPrev ~= 1 then handleTabClick(gfx.mouse_x, gfx.mouse_y) end
      lcPrev = lc
      refreshGeom()
      render()
      gfx.update()
    end
  end

  reaper.defer(loop)
end

shutdown = function()
  winClose(false)
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  setToggle(false)
end

reaper.atexit(shutdown)
setToggle(true)
loop()
