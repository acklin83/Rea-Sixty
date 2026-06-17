-- @description Rea-Sixty — Learn-HUD (ReaImGui spike, place-anywhere + dockable)
-- @author Störsender
-- @version 0.1.0
-- @provides [main] .
-- @about
--   SPIKE / parallel variant of rea_sixty_assignment_hud.lua.
--
--   Same Learn-HUD (per-domain tabs, grouped param list, click-to-learn,
--   layer badge, auto-follow focus) but rendered with **ReaImGui** instead of
--   pure `gfx`. Wins: hi-res/DPI-aware text, freely movable + multi-monitor,
--   ReaImGui's built-in title-bar docking, hover tooltips for free.
--
--   Data layer is ported VERBATIM from the gfx HUD and reads the exact same
--   ExtState the extension publishes:
--     • hud_geom_uc1 / hud_state / hud_assign / hud_learn / hud_hint
--     • writes hud_cmd ("learn;<idx>" / "cancel")
--     • CS/BC colours via overlay_cs_col / overlay_bc_col (shared product-wide)
--   Text size / colour options under their own keys (hud_font / hud_text_white)
--   — SHARED with the gfx HUD on purpose. Window geometry under own keys
--   (hud_imgui_*) so this spike coexists with the shipping gfx HUD.
--
--   Unlike the gfx HUD this window has a TITLE BAR: the list rows are clickable
--   (learn), so dragging the body to move would fight the clicks — the title bar
--   gives clean move/dock instead. Requires ReaImGui (>= 0.9).

local SECT   = "rea_sixty"
local RUNKEY = "hud_imgui_running"

local _, _, sectionID, cmdID = reaper.get_action_context()

if reaper.GetExtState(SECT, RUNKEY) == "1" then
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  return
end
reaper.SetExtState(SECT, RUNKEY, "1", false)
reaper.SetExtState(SECT, "hud_touch_learn", "0", false)   -- start with HW-learn off

local function setToggle(on)
  if sectionID and cmdID and sectionID >= 0 then
    reaper.SetToggleCommandState(sectionID, cmdID, on and 1 or 0)
    reaper.RefreshToolbar2(sectionID, cmdID)
  end
end
setToggle(true)

if not reaper.ImGui_CreateContext then
  reaper.MB("This spike needs ReaImGui (ReaScript ImGui API).\n\n"
    .. "Install via ReaPack: 'ReaImGui: ReaScript binding for Dear ImGui'.",
    "Rea-Sixty Learn-HUD (ImGui)", 0)
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  setToggle(false)
  return
end

local floor = math.floor

------------------------------------------------------------------------
-- Tunables (live via ExtState — shared with the gfx HUD / inserts overlay).
------------------------------------------------------------------------
local function num(key, def)
  local v = tonumber(reaper.GetExtState(SECT, key))
  if v == nil then return def end
  return v
end
local function csRgb() return floor(num("overlay_cs_col", 0xFFFF00)) & 0xFFFFFF end
local function bcRgb() return floor(num("overlay_bc_col", 0xFF0000)) & 0xFFFFFF end

-- Text-size scale for the LIST + parameter-panel body (XS … XL, Medium = 1.0 =
-- the default). The chrome (tab titles, modifier badge, top buttons, TAB_H)
-- stays a FIXED size on purpose — scaling it made the titles balloon. The face
-- view scales to the window and ignores this entirely.
local function fontScale()
  local v = num("hud_imgui_font", 1.0)
  if v < 0.7 then v = 0.7 elseif v > 1.5 then v = 1.5 end
  return v
end

------------------------------------------------------------------------
-- Geometry: parse "hud_geom_uc1" once (cache by raw string). [verbatim]
------------------------------------------------------------------------
local geomRaw = nil
local geom    = { w = 860, h = 660, ctrl = {} }

local function parseGeom(raw)
  local g = { w = 860, h = 660, ctrl = {} }
  local first = true
  for line in raw:gmatch("[^\n]+") do
    if first then
      local dev, w, h = line:match("^(%w+);(%d+);(%d+)")
      if w then g.w = tonumber(w); g.h = tonumber(h) end
      first = false
    else
      local idx, shape, cx, cy, r, w, h, dom, label, rest =
        line:match("^(%d+);(%d+);([%d%.]+);([%d%.]+);([%d%.]+);([%d%.]+);([%d%.]+);(%a);([^;]*);(.*)$")
      if idx then
        -- rest = "section"  OR (new format) "section;<cap RGBA>;<legend>".
        -- Backward compatible: old geometry without cap/legend still parses.
        local sec, cap, legend = rest, 0, ""
        local s2, c2, l2 = rest:match("^([^;]*);(%d+);(.*)$")
        if s2 then sec, cap, legend = s2, tonumber(c2) or 0, l2 end
        local cid = tonumber(idx)
        g.ctrl[cid] = {
          idx = cid,   -- stored as a field too (mockup hit-rects + learn need it)
          shape = tonumber(shape), cx = tonumber(cx), cy = tonumber(cy),
          r = tonumber(r), w = tonumber(w), h = tonumber(h),
          dom = dom, label = label or "", sec = sec or "",
          cap = cap, legend = (legend ~= "" and legend) or (label or ""),
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
-- State + assignments. [verbatim]
------------------------------------------------------------------------
local function readState()
  local raw = reaper.GetExtState(SECT, "hud_state")
  local dev, cs, bc, fdom, layer, csN, bcN =
    raw:match("^(%w+);(%d);(%d);(%a);(%d);([^;]*);(.*)$")
  return {
    dev = dev or "UC1",
    csPresent = (cs == "1"),
    bcPresent = (bc == "1"),
    focusDom = fdom or "n",
    layer = tonumber(layer) or 0,
    csShort = csN or "",
    bcShort = bcN or "",
  }
end

local activeTab    = "cs"
local lastFocusDom = "n"
local tabRects     = {}
local learnBtnRect = nil
local ctrlRects    = {}
local learnIdx     = -1
local ctxCtrlIdx   = -1   -- control under a right-click → control context menu
local ctxCtrlLayer = 0    -- active modifier layer captured AT right-click (the
                          -- modifier is still held then; renaming opens a native
                          -- text dialog that releases it, so we must latch here
                          -- rather than let the extension read the live layer).
-- UF8 device tab (Phase 2): interactive strip-grid.
local uf8Rects     = {}   -- per-cell hit-rects {kind, strip, x, y, w, h}
local uf8BankRects = {}   -- V-Pot bank selector hit-rects {b, x, y, w, h}
local uf8Learn     = -1   -- armed cell, encoded kind*8+strip (hud_uf8_learn), -1 none
local ctxUf8Kind   = -1   -- cell under a right-click → UF8 cell context menu
local ctxUf8Strip  = -1
local ctxUf8Bank   = -1   -- V-Pot bank under a right-click → bank rename menu
local uf8Banks     = {}   -- [0..7] = V-Pot bank label ("" = none)
local uf8BankCols  = {}   -- [0..7] = V-Pot bank colour (0xRRGGBB or nil)
-- The 10 hardware-renderable SSL DAW-Colour swatches (must match
-- selPaletteRgb in Protocol.cpp): red, orange, yellow, green, cyan, blue,
-- purple, magenta, pink, white.
local UF8_BANK_PALETTE = {
  0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00, 0x00FFFF,
  0x0000FF, 0x8000FF, 0xFF00FF, 0xFF0080, 0xFFFFFF,
}
local lastUf8Tab   = nil  -- edge-write "hud_uf8_tab" so C++ auto-engages Plugin Mode
local frame        = 0
local hintText     = ""
local hintFrames   = 0
local scrollY      = 0
local maxScroll    = 0

-- Parameter-list drawer (right-hand panel): pick a param → click a control to
-- assign it (no wiggle). The window GROWS outward by PARAM_PW when the drawer
-- opens (see loop()) so the mockup keeps its size; RW reserves that strip on the
-- right. PARAM_PW is fixed so the grow delta and the reserve match exactly.
local PARAM_PW         = 240
local paramPanelOpen   = false
local RW               = 0
local paramBtnRect     = nil
local paramRects       = {}
local selectedParam    = -1
local selectedParamNm  = ""
local paramFilter      = ""
local paramScroll      = 0
local paramMaxScroll   = 0
local paramCacheKey    = nil
local paramList        = {}

local function sendCmd(s) reaper.SetExtState(SECT, "hud_cmd", s, false) end

local function clamp(v, lo, hi) return math.max(lo, math.min(v, hi)) end

-- Resolve the active domain's plug-in target from the extension's "hud_target"
-- publish ("csTrNum;csFx;bcTrNum;bcFx"; trNum 0 = master, 1-based track, -1 none).
local function resolveTarget()
  local raw = reaper.GetExtState(SECT, "hud_target")
  local csN, csFx, bcN, bcFx = raw:match("^(%-?%d+);(%-?%d+);(%-?%d+);(%-?%d+)$")
  if not csN then return nil end
  local trN, fx
  if activeTab == "cs" then trN, fx = tonumber(csN), tonumber(csFx)
  else                      trN, fx = tonumber(bcN), tonumber(bcFx) end
  if trN < 0 or fx < 0 then return nil end
  local tr = (trN == 0) and reaper.GetMasterTrack(0) or reaper.GetTrack(0, trN - 1)
  if not tr then return nil end
  return tr, fx
end

-- Enumerate the target plug-in's params (cached; names are static per plug-in).
local function getParams(tr, fx)
  local key = tostring(tr) .. ";" .. fx
  if key ~= paramCacheKey then
    paramCacheKey = key
    paramList = {}
    local n = reaper.TrackFX_GetNumParams(tr, fx)
    for p = 0, n - 1 do
      local _, nm = reaper.TrackFX_GetParamName(tr, fx, p, "")
      if nm == nil or nm == "" then nm = "Param " .. p end
      paramList[#paramList + 1] = { p = p, name = nm }
    end
  end
  return paramList
end

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

-- UF8 device tab (read-only strip-grid). State + per-control assignments come on
-- their own ExtState keys (hud_uf8_state / hud_uf8_assign) so they coexist with
-- the UC1 CS/BC payloads. UF8 has no modifier layers.
--   state : "UF8;<present>;<faderBank>;<vpotBank>;<boot>;<focus>;<short>"
--   assign: "<strip>;<kind>;<paramName>;<inv>"  kind 0=V-Pot 1=Fader 2=Solo 3=Cut 4=Sel
-- boot=1 (present=0): no UF8 map but a virgin FX is under the cursor → the grid
-- renders armable em-dash cells so a click + wiggle bootstraps a UF8-only map.
-- focus=1: the cursor IS the shown UF8 plug-in → auto-switch to the UF8 tab.
local function readUf8State()
  local raw = reaper.GetExtState(SECT, "hud_uf8_state")
  local dev, present, fb, vb, boot, focus, short =
    raw:match("^(%w+);(%d);(%d);(%d);(%d);(%d);(.*)$")
  return {
    present   = (present == "1"),
    boot      = (boot == "1"),
    focus     = (focus == "1"),
    faderBank = tonumber(fb) or 0,
    vpotBank  = tonumber(vb) or 0,
    short     = short or "",
  }
end

local function readUf8Assign()
  local raw = reaper.GetExtState(SECT, "hud_uf8_assign")
  local m = {}   -- m[strip][kind] = { name, inv, mode }  (mode: V-Pot 0/1)
  for line in raw:gmatch("[^\n]+") do
    local strip, kind, name, inv, mode = line:match("^(%d+);(%d+);([^;]*);(%d);(%d)$")
    if not strip then   -- tolerate the old 4-field form (no mode)
      strip, kind, name, inv = line:match("^(%d+);(%d+);([^;]*);(%d)$"); mode = "0"
    end
    if strip then
      local s, k = tonumber(strip), tonumber(kind)
      m[s] = m[s] or {}
      m[s][k] = { name = name or "", inv = (inv == "1"), mode = tonumber(mode) or 0 }
    end
  end
  return m
end

-- V-Pot bank labels + colours (per Top-Soft-Key bank). Interleaved
-- "label0;RRGGBB0;label1;RRGGBB1;…" (16 fields). Returns label table + colour
-- table; labels stay plain strings so existing consumers are unchanged.
local function readUf8Banks()
  local raw = reaper.GetExtState(SECT, "hud_uf8_banks")
  local fields = {}
  for field in (raw .. ";"):gmatch("([^;]*);") do fields[#fields + 1] = field end
  local labels, cols = {}, {}
  for b = 0, 7 do
    labels[b] = fields[b * 2 + 1] or ""
    cols[b]   = tonumber(fields[b * 2 + 2] or "", 16)
  end
  return labels, cols
end

local SECTION_ORDER = {
  cs = { "Filter", "EQ", "Dynamics", "Gate", "I/O & Channel" },
  bc = { "Bus Comp" },
}
local COLUMN_SECTIONS = {
  cs = { { "Filter", "EQ", "I/O & Channel" }, { "Dynamics", "Gate" } },
  bc = { { "Bus Comp" } },
}

local function groupControls(domChar)
  local by = {}
  for idx, c in pairs(geom.ctrl) do
    if c.dom == domChar then
      c.idx = idx
      local s = (c.sec ~= "" and c.sec) or "Other"
      local list = by[s]; if not list then list = {}; by[s] = list end
      list[#list + 1] = c
    end
  end
  for _, list in pairs(by) do
    table.sort(list, function(a, b) return a.idx < b.idx end)
  end
  return by
end

------------------------------------------------------------------------
-- ImGui context + draw abstraction. gfx primitives → DrawList.
--   gfx.set/gset    → col(rgb,a)        (0xRRGGBB + 0..1 alpha → 0xRRGGBBAA)
--   gfx.rect        → rect(x,y,w,h,col) (window-local coords)
--   gfx.drawstr     → dtext(x,y,col,s,px)
--   gfx.measurestr  → measure(s,px) → w,h
-- All coords are window-local; OX/OY/WW/WH (set each frame) map to screen.
------------------------------------------------------------------------
local ctx  = reaper.ImGui_CreateContext('Rea-Sixty Learn-HUD')
local font = reaper.ImGui_CreateFont('sans-serif', 0)   -- v0.10: size at push time
reaper.ImGui_Attach(ctx, font)

local dl, OX, OY, WW, WH

local function col(rgb, a)
  local al = floor((a or 1) * 255 + 0.5)
  if al < 0 then al = 0 elseif al > 255 then al = 255 end
  return ((rgb & 0xFFFFFF) << 8) | al
end

local function rect(x, y, w, h, c)
  reaper.ImGui_DrawList_AddRectFilled(dl, OX + x, OY + y, OX + x + w, OY + y + h, c, 0)
end

local function dtext(x, y, c, s, px)
  reaper.ImGui_PushFont(ctx, font, px)
  reaper.ImGui_DrawList_AddText(dl, OX + x, OY + y, c, s)
  reaper.ImGui_PopFont(ctx)
end

local function measure(s, px)
  reaper.ImGui_PushFont(ctx, font, px)
  local w, h = reaper.ImGui_CalcTextSize(ctx, s)
  reaper.ImGui_PopFont(ctx)
  return w, h
end

------------------------------------------------------------------------
-- Render. [ported from gfx, using the draw abstraction]
------------------------------------------------------------------------
local TAB_H = 36

local function fit(s, maxw, px)
  if s == "" then return s end
  local w = measure(s, px)
  if w <= maxw then return s end
  while #s > 1 do
    s = s:sub(1, #s - 1)
    if measure(s .. "\xE2\x80\xA6", px) <= maxw then return s .. "\xE2\x80\xA6" end
  end
  return s
end

local function drawTabs(st, ust)
  rect(0, 0, WW, TAB_H, col(0x171719, 1))
  tabRects = {}
  local function tab(dom, present, x)
    local label = (dom == "cs") and "Channel Strip"
              or  (dom == "bc") and "Bus Comp"
              or  "UF8"
    local px    = 10   -- fixed chrome, matches the top-right toggle buttons
    local w, h  = measure(label, px); w = w + 28
    local active = (activeTab == dom)
    local rgb    = (dom == "cs") and csRgb()
               or  (dom == "bc") and bcRgb()
               or  0x4A90D8                          -- UF8 accent (blue)
    if active then rect(x, 4, w, TAB_H - 6, col(rgb, present and 0.30 or 0.16))
    else           rect(x, 4, w, TAB_H - 6, col(0x242429, 1)) end
    if active then rect(x, 4, w, 3, col(rgb, present and 1 or 0.5)) end
    dtext(x + 12, 4 + floor((TAB_H - 6 - h) / 2), col(rgb, present and 1 or 0.40), label, px)
    tabRects[#tabRects + 1] = { dom = dom, x = x, y = 4, w = w, h = TAB_H - 6 }
    return x + w + 4
  end
  local x = 8
  x = tab("cs",  st.csPresent,            x)
  x = tab("bc",  st.bcPresent,            x)
  x = tab("uf8", ust and ust.present,     x)

  -- The modifier badge + Touch-to-Learn / Parameter-List toggles are UC1-only
  -- (UF8 has no layers and the device tab is read-only), so skip them on UF8.
  if activeTab == "uf8" then
    learnBtnRect, paramBtnRect = nil, nil
    return
  end

  -- Active modifier-layer badge (far right).
  local name = (st.layer == 1 and "OPT") or (st.layer == 2 and "CTRL")
            or (st.layer == 3 and "C+O") or "NORM"
  local px   = 10   -- fixed chrome (badge + the top toggle buttons)
  local bwm, bhm = measure(name, px)
  local bw = bwm + 20
  local bh = TAB_H - 12
  local bx = WW - bw - 8
  local by = 6
  if st.layer == 0 then
    rect(bx, by, bw, bh, col(0x29292E, 1))
    dtext(bx + 10, by + floor((bh - bhm) / 2), col(0x8890A0, 0.7), name, px)
  else
    local lc = (st.layer == 1 and 0x30C8A0)
            or (st.layer == 2 and 0xC878FF)
            or 0xE0A040   -- C+O = amber
    rect(bx, by, bw, bh, col(lc, 0.92))
    dtext(bx + 10, by + floor((bh - bhm) / 2), col(0x121214, 1), name, px)
  end

  -- Toggle buttons walking LEFT from the modifier badge: Touch-to-Learn, then
  -- Parameter List. Both also have a right-click-menu entry.
  local rx = bx
  local function rightToggle(lbl, on, onRgb)
    local lw, lh = measure(lbl, px)
    local w = lw + 20
    local tx = rx - w - 6
    if on then
      rect(tx, by, w, bh, col(onRgb, 0.95))
      dtext(tx + 10, by + floor((bh - lh) / 2), col(0x161208, 1), lbl, px)
    else
      rect(tx, by, w, bh, col(0x29292E, 1))
      reaper.ImGui_DrawList_AddRect(dl, OX + tx, OY + by, OX + tx + w, OY + by + bh,
        col(0x4A5060, 1), 0, 0, 1)
      dtext(tx + 10, by + floor((bh - lh) / 2), col(0x9098A4, 0.85), lbl, px)
    end
    rx = tx
    return { x = tx, y = by, w = w, h = bh }
  end

  learnBtnRect = rightToggle("Touch to Learn",
    reaper.GetExtState(SECT, "hud_touch_learn") == "1", 0xE0A838)   -- amber
  paramBtnRect = rightToggle("Parameter List",
    reaper.GetExtState(SECT, "hud_imgui_params") == "1", 0x4A90D8)  -- blue
end

local function hitRect(r, mx, my)
  return r and mx >= r.x and mx <= r.x + r.w and my >= r.y and my <= r.y + r.h
end

local function handleLearnBtnClick(mx, my)
  if hitRect(learnBtnRect, mx, my) then
    local on = (reaper.GetExtState(SECT, "hud_touch_learn") == "1")
    reaper.SetExtState(SECT, "hud_touch_learn", on and "0" or "1", false)
    return true
  end
  return false
end

local function handleParamBtnClick(mx, my)
  if hitRect(paramBtnRect, mx, my) then
    local on = (reaper.GetExtState(SECT, "hud_imgui_params") == "1")
    reaper.SetExtState(SECT, "hud_imgui_params", on and "0" or "1", true)
    return true
  end
  return false
end

local function handleTabClick(mx, my)
  for _, t in ipairs(tabRects) do
    if mx >= t.x and mx <= t.x + t.w and my >= t.y and my <= t.y + t.h then
      if t.dom ~= activeTab then selectedParam = -1 end   -- param idx is per-FX
      activeTab = t.dom
      return true
    end
  end
  return false
end

-- Control idx under (mx,my) using the last frame's hit-rects (knob = circle,
-- toggle/btn/list-row = box). Returns nil when the point is over no control.
local function controlAt(mx, my)
  for _, h in ipairs(ctrlRects) do
    local hit
    if h.shape == 0 then
      local dx, dy = mx - h.x, my - h.y
      hit = (dx * dx + dy * dy) <= (h.r + 3) * (h.r + 3)
    else
      hit = mx >= h.x and mx <= h.x + h.w and my >= h.y and my <= h.y + h.h
    end
    if hit then return h.idx end
  end
  return nil
end

-- UF8 grid cell under (mx,my) → {kind, strip}, or nil. Uses last frame's rects.
local function uf8CellAt(mx, my)
  for _, h in ipairs(uf8Rects) do
    if mx >= h.x and mx <= h.x + h.w and my >= h.y and my <= h.y + h.h then
      return h.kind, h.strip
    end
  end
  return nil
end

-- V-Pot bank cell under (mx,my) → bank index 0..7, or nil.
local function uf8BankAt(mx, my)
  for _, h in ipairs(uf8BankRects) do
    if mx >= h.x and mx <= h.x + h.w and my >= h.y and my <= h.y + h.h then
      return h.b
    end
  end
  return nil
end

local function handleControlClick(mx, my)
  local idx = controlAt(mx, my)
  if not idx then
    -- Click on empty content area → cancel any armed learn (easy mouse escape).
    if learnIdx >= 0 and selectedParam < 0 then sendCmd("cancel") end
    return false
  end
  if selectedParam >= 0 then
    -- Param picked in the list → assign it straight to this control.
    sendCmd("bind;" .. idx .. ";" .. selectedParam)
    selectedParam = -1
  elseif learnIdx == idx then sendCmd("cancel")
  else                        sendCmd("learn;" .. idx) end
  return true
end

-- UF8 grid: left-click a cell → arm a learn (click the armed cell again to
-- cancel). The wiggle then binds via the extension. Banks are resolved C++-side.
local function handleUf8CellClick(mx, my)
  local kind, strip = uf8CellAt(mx, my)
  if not kind then return false end
  if uf8Learn == (kind * 8 + strip) then sendCmd("uf8cancel")
  else                                   sendCmd("uf8learn;" .. kind .. ";" .. strip) end
  return true
end

local function handleParamClick(mx, my)
  for _, h in ipairs(paramRects) do
    if mx >= h.x and mx <= h.x + h.w and my >= h.y and my <= h.y + h.h then
      if selectedParam == h.p then
        selectedParam = -1
      else
        selectedParam, selectedParamNm = h.p, h.name
      end
      return true
    end
  end
  return false
end

local function drawRow(c, x, y, rowW, labelW, lineH, rowFont, present, rgb, asn)
  local a        = asn[c.idx]
  local mapped   = (a ~= nil)
  local learning = (c.idx == learnIdx)

  if learning then
    local pulse = 0.22 + 0.22 * math.abs((frame % 50) / 25 - 1)
    rect(x - 3, y - 1, rowW + 6, lineH, col(0xFFFFFF, pulse))
  end

  local labCol, labA
  if mapped       then labCol, labA = rgb,      1.0
  elseif present  then labCol, labA = 0xC6CDD6, 0.95
  else                 labCol, labA = 0x808892, 0.5 end
  dtext(x, y + 1, col(labCol, labA), c.label, rowFont)

  local txt, pCol, pA
  if mapped then
    txt = a.name; if a.inv then txt = txt .. "  inv" end
    pCol, pA = rgb, 1.0
  elseif present then txt, pCol, pA = "\xE2\x80\x94", 0x9097A0, 0.7
  else                txt, pCol, pA = "\xE2\x80\x94", 0x707880, 0.45 end
  dtext(x + labelW, y + 1, col(pCol, pA), fit(txt, rowW - labelW, rowFont), rowFont)

  ctrlRects[#ctrlRects + 1] =
    { idx = c.idx, shape = 1, x = x - 3, y = y - 1, w = rowW + 6, h = lineH }
end

local function renderList(st, asn)
  local domChar = (activeTab == "cs") and "c" or "b"
  local present = (activeTab == "cs") and st.csPresent or st.bcPresent
  local rgb     = (activeTab == "cs") and csRgb() or bcRgb()
  if reaper.GetExtState(SECT, "hud_text_white") == "1" then rgb = 0xFFFFFF end
  local by      = groupControls(domChar)

  local fs       = fontScale()
  local rowFont  = floor(16 * fs + 0.5)
  local headFont = floor(15 * fs + 0.5)

  local M      = 14
  local top    = TAB_H + 10
  local bottom = WH - 8
  if (WW - RW) - 2 * M < 60 or bottom - top < 40 then return end

  local _, rowTH = measure("Ag", rowFont)
  local lineH  = rowTH + floor(5 * fs + 0.5)
  local labelW = 0
  for _, list in pairs(by) do
    for _, c in ipairs(list) do
      local w = measure(c.label, rowFont)
      if w > labelW then labelW = w end
    end
  end
  labelW = labelW + floor(18 * fs + 0.5)
  local paramW = measure("Threshold", rowFont)
  for _, a in pairs(asn) do
    local w = measure(a.name or "", rowFont)
    if w > paramW then paramW = w end
  end
  local colW = labelW + paramW + floor(20 * fs + 0.5)
  local _, headTH = measure("Ag", headFont)
  local headH  = headTH + floor(11 * fs + 0.5)
  local gap    = floor(8 * fs + 0.5)
  local colGap = floor(16 * fs + 0.5)

  local cols = COLUMN_SECTIONS[activeTab] or { SECTION_ORDER[activeTab] or {} }
  local assigned = {}
  for _, cset in ipairs(cols) do for _, s in ipairs(cset) do assigned[s] = true end end
  local leftover = {}
  for _, s in ipairs(SECTION_ORDER[activeTab] or {}) do
    if by[s] and not assigned[s] then leftover[#leftover + 1] = s; assigned[s] = true end
  end
  for s in pairs(by) do
    if not assigned[s] then leftover[#leftover + 1] = s; assigned[s] = true end
  end

  local items, peakY = {}, top
  for ci, cset in ipairs(cols) do
    local x = M + (ci - 1) * (colW + colGap)
    local y = top
    local secList = cset
    if ci == #cols and #leftover > 0 then
      secList = {}
      for _, s in ipairs(cset)     do secList[#secList + 1] = s end
      for _, s in ipairs(leftover) do secList[#secList + 1] = s end
    end
    for _, s in ipairs(secList) do
      local ctrls = by[s]
      if ctrls and #ctrls > 0 then
        items[#items + 1] = { kind = "head", x = x, y = y, text = s }
        y = y + headH
        for _, c in ipairs(ctrls) do
          items[#items + 1] = { kind = "row", x = x, y = y, c = c }
          y = y + lineH
        end
        y = y + gap
      end
    end
    if y > peakY then peakY = y end
  end

  maxScroll = math.max(0, peakY - bottom)
  scrollY = math.max(0, math.min(scrollY, maxScroll))

  ctrlRects = {}
  for _, it in ipairs(items) do
    local yy = it.y - scrollY
    if yy + lineH >= top and yy <= bottom then
      if it.kind == "head" then
        dtext(it.x, yy, col(rgb, present and 0.9 or 0.5), it.text, headFont)
        local _, th = measure(it.text, headFont)
        rect(it.x, yy + th + 2, colW, 1, col(rgb, present and 0.30 or 0.18))
      else
        drawRow(it.c, it.x, yy, colW, labelW, lineH, rowFont, present, rgb, asn)
      end
    end
  end
end

-- ===================================================================
-- Settings-schematic face (unified UC1 mockup) — mirrors drawUc1Face_
-- (SettingsScreen.cpp) 1:1: landscape 860×660, BOTH domains at once with the
-- inactive one dimmed, full silk labels + chassis / GR-meter / LCD decoration.
-- Bindable controls come from the published geometry (cap + mapped state via
-- the live assignments); click-to-learn + tooltip ride on top. Non-bindable
-- hardware keys (SOLO/CUT/FINE/SOLO CLR) are intentionally omitted so the
-- bottom breathes. Layout numbers track analysis/settings_face_reference.py.
-- ===================================================================

-- idx → on-face silk label (the settings face uses full words, not the short
-- HUD legends). idx = kUc1Controls array order = the published geometry key.
local FACE_LABEL = {
  [0]="LO-PASS",[1]="HI-PASS",[2]="BELL",[3]="GAIN",[4]="FREQ",[5]="GAIN",
  [6]="FREQ",[7]="Q",[8]="TYPE",[9]="IN",[10]="GAIN",[11]="FREQ",[12]="Q",
  [13]="FREQ",[14]="GAIN",[15]="BELL",[16]="INPUT",[17]="OUTPUT",
  [18]="THR",[19]="MAKE-UP",[20]="ATTACK",[21]="RELEASE",[22]="RATIO",
  [23]="IN",[24]="S/C HPF",[25]="MIX",[26]="FAST ATK",[27]="PEAK",
  [28]="RATIO",[29]="THRESHOLD",[30]="RELEASE",[31]="IN",[32]="RANGE",
  [33]="THRESHOLD",[34]="RELEASE",[35]="HOLD",[36]="EXPAND",[37]="FAST ATK",
  [38]="\xC3\x98",[39]="S/C LISTEN",[40]="IN",
}
-- Per-control position overrides. As of 2026-06-16 the SSL-style layout is
-- baked into the extension's kUc1Controls (single source for the published
-- geometry + the settings schematic), so the HUD draws straight from the
-- published positions — no overrides needed. Kept (empty) as the hook for any
-- future HUD-only nudge.
local FACE_OV = {}
-- Light (white, dark-text) toggle keys like the settings face's IN buttons.
local FACE_LIGHT = { [23]=true, [40]=true }

-- LCD content: real focused-track data. Prefer the extension's "hud_lcd"
-- publish ("seg;line1;line2;line3"); fall back to the last-touched track so it
-- shows real data even before the C++ side ships.
local function lcdLines()
  local raw = reaper.GetExtState(SECT, "hud_lcd")
  if raw ~= "" then
    local s, a, b, c = raw:match("^([^;]*);([^;]*);([^;]*);(.*)$")
    if s then return s, a, b, c end
  end
  -- Fallback (extension hasn't published yet): no active-FX info here, so
  -- line 3 stays blank rather than showing the old Stereo/Mono.
  local tr = reaper.GetLastTouchedTrack()
  if not tr then return "--", "TRACK", "(no track)", "" end
  local num = reaper.GetMediaTrackInfo_Value(tr, "IP_TRACKNUMBER")
  local _, name = reaper.GetSetMediaTrackInfo_String(tr, "P_NAME", "", false)
  local seg, top
  if num == -1 then
    seg, top = "M", "MASTER"; if name == "" then name = "Master" end
  else
    seg, top = string.format("%03d", floor(num)), "TRACK"
    if name == "" then name = "Track " .. floor(num) end
  end
  return seg, top, name, ""
end

local function renderFace(st, asn)
  local DW, DH = geom.w or 860, geom.h or 660
  local M, top = 8, TAB_H + 6
  local availW, availH = (WW - RW) - 2 * M, WH - top - M
  if availW < 60 or availH < 60 then return end
  local scale = math.min(availW / DW, availH / DH)
  local ox = M + (availW - DW * scale) / 2
  local oy = top + (availH - DH * scale) / 2
  local function X(v) return ox + v * scale end
  local function Y(v) return oy + v * scale end

  local whiteTxt = (reaper.GetExtState(SECT, "hud_text_white") == "1")
  local capCS, capBC = csRgb(), bcRgb()
  local brightDom = (activeTab == "cs") and "c" or "b"
  local function da(dom)              -- dim alpha: inactive domain fades back
    if dom == nil then return 1.0 end
    return (dom == brightDom) and 1.0 or 0.30
  end
  local function presentOf(dom)
    if dom == "c" then return st.csPresent elseif dom == "b" then return st.bcPresent end
    return true
  end

  local lf = math.max(7, floor(11 * scale + 0.5))   -- control / label font
  local sf = math.max(8, floor(12 * scale + 0.5))   -- section-header font
  local mx, my = reaper.ImGui_GetMousePos(ctx)
  local lmx, lmy = mx - OX, my - OY
  local tipSlot, tipParam
  ctrlRects = {}

  -- design-space draw helpers ------------------------------------------
  local function dRect(x, y, w, h, fill, outl, rounding)
    local rd = (rounding or 3) * scale
    reaper.ImGui_DrawList_AddRectFilled(dl, OX + X(x), OY + Y(y), OX + X(x + w), OY + Y(y + h), fill, rd)
    if outl then reaper.ImGui_DrawList_AddRect(dl, OX + X(x), OY + Y(y), OX + X(x + w), OY + Y(y + h), outl, rd, 0, math.max(1, scale)) end
  end
  local function dLine(x1, y1, x2, y2, c, w)
    reaper.ImGui_DrawList_AddLine(dl, OX + X(x1), OY + Y(y1), OX + X(x2), OY + Y(y2), c, math.max(1, (w or 1) * scale))
  end
  local function dTextC(cx, cy, c, s, px)
    local tw, th = measure(s, px); dtext(X(cx) - tw / 2, Y(cy) - th / 2, c, s, px)
  end
  local function dTextL(x, y, c, s, px) dtext(X(x), Y(y), c, s, px) end
  local function dTextLV(x, cy, c, s, px)   -- left-aligned, vertically centred on cy
    local _, th = measure(s, px); dtext(X(x), Y(cy) - th / 2, c, s, px)
  end
  local function dTextRB(xr, yb, c, s, px)  -- right edge xr, bottom edge yb
    local tw, th = measure(s, px); dtext(X(xr) - tw, Y(yb) - th, c, s, px)
  end

  -- bindable knob ------------------------------------------------------
  local function knob(idx, cx, cy, rD, cap, dom)
    local f = da(dom); local present = presentOf(dom)
    local mapped = idx and asn[idx] ~= nil
    local learning = idx and idx == learnIdx
    local capCol = (cap and cap > 0) and ((cap >> 8) & 0xFFFFFF) or ((dom == "b") and capBC or capCS)
    local r = rD * scale; local sx, sy = X(cx), Y(cy)
    reaper.ImGui_DrawList_AddCircleFilled(dl, OX + sx, OY + sy, r, col(0x14181E, f))
    reaper.ImGui_DrawList_AddCircle(dl, OX + sx, OY + sy, r, col(0x4A5060, f), 0, math.max(1, r * 0.10))
    local capA = mapped and 1.0 or (present and 0.7 or 0.4)
    reaper.ImGui_DrawList_AddCircleFilled(dl, OX + sx, OY + sy, r * 0.78, col(capCol, capA * f))
    if mapped then reaper.ImGui_DrawList_AddCircle(dl, OX + sx, OY + sy, r + 2 * scale, col(capCol, 0.9 * f), 0, math.max(1, r * 0.12)) end
    if learning then
      local p = 0.4 + 0.4 * math.abs((frame % 50) / 25 - 1)
      reaper.ImGui_DrawList_AddCircle(dl, OX + sx, OY + sy, r + 3 * scale, col(0xFFFFFF, p), 0, math.max(1, r * 0.14))
    end
    reaper.ImGui_DrawList_AddLine(dl, OX + sx, OY + sy - r * 0.85, OX + sx, OY + sy - r * 0.45, col(0xE8E8E8, (present and 0.9 or 0.5) * f), math.max(1, r * 0.13))
    local lbl = idx and FACE_LABEL[idx]
    if lbl then dTextC(cx, cy + rD + 12, col(0xB8BCC4, (present and 0.95 or 0.6) * f), lbl, lf) end
    if idx then
      ctrlRects[#ctrlRects + 1] = { idx = idx, shape = 0, x = sx, y = sy, r = r }
      local dx, dy = lmx - sx, lmy - sy
      if dx * dx + dy * dy <= (r + 3) * (r + 3) then
        local g = geom.ctrl[idx]; tipSlot = (g and g.label) or lbl
        tipParam = mapped and (asn[idx].name .. (asn[idx].inv and "  (inverted)" or "")) or nil
      end
    end
  end

  -- bindable / labelled button ----------------------------------------
  local function btn(idx, x, y, wD, hD, label, dom, light)
    local f = da(dom); local present = presentOf(dom)
    local mapped = idx and asn[idx] ~= nil
    local learning = idx and idx == learnIdx
    local sx, sy = X(x), Y(y); local sw, sh = wD * scale, hD * scale
    local capCol = (dom == "b") and capBC or capCS
    local fill, txtC, outl
    if light then
      fill = col(0xE0E0E0, (present and 1 or 0.6) * f)
      txtC = col(0x303338, f)
      outl = col(mapped and capCol or 0x808088, f)
    else
      fill = mapped and col(capCol, 0.30 * f) or col(0x252A33, f)
      local tc = mapped and (whiteTxt and 0xFFFFFF or capCol) or (present and 0xC0C4CC or 0x707880)
      txtC = col(tc, (present and 1 or 0.6) * f)
      outl = col(mapped and capCol or 0x4A5060, f)
    end
    reaper.ImGui_DrawList_AddRectFilled(dl, OX + sx, OY + sy, OX + sx + sw, OY + sy + sh, fill, 3 * scale)
    reaper.ImGui_DrawList_AddRect(dl, OX + sx, OY + sy, OX + sx + sw, OY + sy + sh, outl, 3 * scale, 0, math.max(1, scale))
    if learning then
      local p = 0.4 + 0.4 * math.abs((frame % 50) / 25 - 1)
      reaper.ImGui_DrawList_AddRect(dl, OX + sx - 2, OY + sy - 2, OX + sx + sw + 2, OY + sy + sh + 2, col(0xFFFFFF, p), 3 * scale, 0, math.max(1, 2 * scale))
    end
    local shown = fit(label, sw - 4, lf)
    local tw, th = measure(shown, lf)
    dtext(sx + (sw - tw) / 2, sy + (sh - th) / 2, txtC, shown, lf)
    if idx then
      ctrlRects[#ctrlRects + 1] = { idx = idx, shape = 1, x = sx, y = sy, w = sw, h = sh }
      if lmx >= sx and lmx <= sx + sw and lmy >= sy and lmy <= sy + sh then
        local g = geom.ctrl[idx]; tipSlot = (g and g.label) or label
        tipParam = mapped and (asn[idx].name .. (asn[idx].inv and "  (inverted)" or "")) or nil
      end
    end
  end

  local function section(x, y, text, dom) dTextL(x, y, col(0x9CA0AA, da(dom)), text, sf) end
  local function decoKnob(cx, cy, rD, label)
    local r = rD * scale; local sx, sy = X(cx), Y(cy)
    reaper.ImGui_DrawList_AddCircleFilled(dl, OX + sx, OY + sy, r, col(0x14181E, 1))
    reaper.ImGui_DrawList_AddCircle(dl, OX + sx, OY + sy, r, col(0x4A5060, 1), 0, math.max(1, r * 0.10))
    reaper.ImGui_DrawList_AddCircleFilled(dl, OX + sx, OY + sy, r * 0.78, col(0x6A707C, 0.85))
    reaper.ImGui_DrawList_AddLine(dl, OX + sx, OY + sy - r * 0.85, OX + sx, OY + sy - r * 0.45, col(0xE8E8E8, 0.8), math.max(1, r * 0.13))
    dTextC(cx, cy + rD + 12, col(0xB8BCC4, 0.85), label, lf)
  end
  local function decoBtn(x, y, wD, hD, label)
    dRect(x, y, wD, hD, col(0x252A33, 1), col(0x4A5060, 1), 3)
    local tw, th = measure(label, lf)
    dtext(X(x) + (wD * scale - tw) / 2, Y(y) + (hD * scale - th) / 2, col(0xC0C4CC, 1), label, lf)
  end

  -- ===== Chassis + column frames =====
  dRect(4, 4, DW - 8, DH - 8, col(0x14181E, 0.0), col(0x2A3038, 1), 8)
  dRect(12, 12, 230, DH - 24, col(0x1A1E24, da("c")), col(0x2A3038, da("c")), 6)         -- left (EQ)
  dRect(250, 12, 360, 420, col(0x1A1E24, da("b")), col(0x2A4870, da("b")), 6)            -- centre BC
  dRect(618, 12, 230, DH - 24, col(0x1A1E24, da("c")), col(0x2A3038, da("c")), 6)        -- right (Dyn/Chan)
  dRect(250, 440, 360, 208, col(0x1A1E24, 1), col(0x903030, 1), 6)                       -- CCP (neutral)

  -- ===== Section + side labels =====
  local fC = da("c")
  section(26, 104, "FILTERS", "c")
  dLine(70, 122, 234, 122, col(0x9DA2AC, 0.6 * fC), 1.0)   -- match band divider lines
  -- Band labels aligned to each band's FREQ knob height.
  dTextLV(26, 188, col(0xB8BCC4, fC), "HF", lf)
  dTextLV(26, 288, col(0xB8BCC4, fC), "HMF", lf)
  dTextLV(26, 379, col(0xB8BCC4, fC), "EQ", lf)   -- left of the HMF|LMF (Type/In) divider
  dTextLV(26, 458, col(0xB8BCC4, fC), "LMF", lf)
  dTextLV(26, 558, col(0xB8BCC4, fC), "LF", lf)
  section(632, 26, "COMPRESSOR", "c")
  -- GATE / EXPANDER — right-aligned, bottom flush with the FAST ATK button (510).
  dTextRB(834, 492, col(0x9CA0AA, fC), "GATE /", sf)
  dTextRB(834, 510, col(0x9CA0AA, fC), "EXPANDER", sf)
  section(632, 518, "CHANNEL", "c")
  dTextC(430, 22, col(0x9CA0AA, da("b")), "BUS COMP", sf)

  -- ===== BC GR meter (BC domain) =====
  local fB = da("b")
  local mw, mh = 196, 80
  local mxg, myg = 250 + (360 - mw) / 2, 44
  dRect(mxg, myg, mw, mh, col(0x141416, fB), col(0x282A2E, fB), 4)
  dRect(mxg + 4, myg + 4, mw - 8, mh - 8, col(0x080C12, fB), col(0x444A55, fB), 2)
  local mcx, mcy, ra = mxg + mw / 2, myg + mh - 3, 70
  local a0, a1 = math.rad(-130), math.rad(-50)
  local function dBtoA(dbv) return a0 + (dbv / 20) * (a1 - a0) end
  for _, t in ipairs({ { 0, "0" }, { 5, "5" }, { 10, "10" }, { 15, "15" }, { 20, "20" } }) do
    local a = dBtoA(t[1])
    dLine(mcx + math.cos(a) * ra, mcy + math.sin(a) * ra, mcx + math.cos(a) * (ra - 8), mcy + math.sin(a) * (ra - 8), col(0x4499DD, fB), 1.6)
    dTextC(mcx + math.cos(a) * (ra - 16), mcy + math.sin(a) * (ra - 16), col(0x4499DD, fB), t[2], lf)
  end
  local aN = dBtoA(7)
  dLine(mcx, mcy, mcx + math.cos(aN) * (ra - 4), mcy + math.sin(aN) * (ra - 4), col(0x4499DD, fB), 2.0)
  dTextC(mcx, myg + mh - 12, col(0x4499DD, fB), "GR", lf)

  -- ===== Comp GR LED meter — 2 dot columns + dB number, in col2 (cx=774) =====
  for i, s in ipairs({ "20", "14", "10", "6", "3" }) do
    local ly = 220 + (i - 1) * 13
    dTextC(760, ly, col(0x808088, fC), s, lf)
    reaper.ImGui_DrawList_AddCircleFilled(dl, OX + X(780), OY + Y(ly), math.max(1, 3 * scale), col(0x9A5A2A, fC))
    reaper.ImGui_DrawList_AddCircleFilled(dl, OX + X(794), OY + Y(ly), math.max(1, 3 * scale), col(0x2E8040, fC))
  end

  -- ===== Central Control Panel: 7-seg + LCD (real data) + buttons =====
  local seg, l1, l2, l3 = lcdLines()
  local kCcpY = 440
  dRect(264, kCcpY + 14, 56, 30, col(0x1A0408, 1), col(0x401818, 1), 3)
  dTextC(292, kCcpY + 28, col(0xFF3030, 1), seg, math.max(9, floor(18 * scale + 0.5)))
  local lcdX, lcdW = 328, 222; local lcdCx = lcdX + lcdW / 2
  dRect(lcdX, kCcpY + 12, lcdW, 76, col(0x05080C, 1), col(0x444A55, 1), 3)
  dTextC(lcdCx, kCcpY + 26, col(0x808088, 1), l1, lf)
  dTextC(lcdCx, kCcpY + 46, col(0xE0E0E0, 1), fit(l2, lcdW * scale - 8, math.max(9, floor(13 * scale + 0.5))), math.max(9, floor(13 * scale + 0.5)))
  dTextC(lcdCx, kCcpY + 66, col(0x4488DD, 1), fit(l3, lcdW * scale - 8, lf), lf)
  do
    local bw, bh, gap = 80, 22, 20; local total = 2 * bw + gap
    local x0, y0 = 250 + (360 - total) / 2, kCcpY + 100
    decoBtn(x0, y0, bw, bh, "360"); decoBtn(x0 + bw + gap, y0, bw, bh, "MAGNIFY")
  end
  decoKnob(390, kCcpY + 145, 18, "CS Encoder")
  decoKnob(470, kCcpY + 145, 18, "BC Encoder")
  dTextC(430, kCcpY + 208 - 14, col(0x707880, 1), "Rea-Sixty", lf)

  -- ===== EQ band divider lines — stepped (horizontal · 45° · horizontal) to
  -- follow the staggered 2-column knob layout, like the SSL 4000E silk. =====
  do
    local dc = col(0x9DA2AC, 0.6 * da("c"))
    local th = math.max(1, scale)
    local function poly(pts)
      for i = 1, #pts - 1 do
        reaper.ImGui_DrawList_AddLine(dl, OX + X(pts[i][1]), OY + Y(pts[i][2]),
          OX + X(pts[i + 1][1]), OY + Y(pts[i + 1][2]), dc, th)
      end
    end
    local gxR, fxR = 102, 182          -- GAIN col right edge / FREQ col right edge (c2=162)
    poly({ { 62, 210 }, { gxR, 210 }, { gxR + 28, 238 }, { fxR, 238 } })  -- HF | HMF (down)
    poly({ { 62, 379 }, { 143, 379 } })                                   -- HMF | LMF → IN/TYPE
    poly({ { 62, 548 }, { gxR, 548 }, { gxR + 40, 508 }, { fxR, 508 } })  -- LMF | LF (up)
  end

  -- ===== Bindable controls from the published geometry =====
  for idx, g in pairs(geom.ctrl) do
    local ov = FACE_OV[idx]
    if g.shape == 0 then
      knob(idx, (ov and ov.cx) or g.cx, (ov and ov.cy) or g.cy, g.r, g.cap, g.dom)
    else
      local x = (ov and ov.x) or g.cx
      local y = (ov and ov.y) or g.cy
      local w = (ov and ov.w) or g.w
      local h = (ov and ov.h) or g.h
      btn(idx, x, y, w, h, FACE_LABEL[idx] or g.legend, g.dom, FACE_LIGHT[idx])
    end
  end

  if tipSlot then
    local t = tipSlot
    if tipParam then t = t .. "  \xE2\x80\x94  " .. tipParam end
    reaper.ImGui_SetTooltip(ctx, t)
  end
end

-- ===================================================================
-- Parameter-list drawer. Right-hand panel listing every param of the active
-- domain's plug-in; click a row to arm it, then click a control to bind it.
-- Type-to-filter (no InputText widget — fits the DrawList draw model + dodges
-- the standalone-focus InputText trap). Mapped params get a green dot.
-- ===================================================================
local function renderParamPanel(st, asn)
  local PW = RW
  local x0  = WW - PW
  local top = TAB_H
  local fs  = fontScale()
  local hf  = floor(13 * fs + 0.5)
  local rf  = floor(14 * fs + 0.5)
  local pad = floor(8 * fs + 0.5)

  rect(x0, top, PW, WH - top, col(0x16171A, 0.97))
  rect(x0, top, 2, WH - top, col(0x303440, 1))    -- left edge seam

  paramRects = {}
  local tr, fx = resolveTarget()
  if not tr then
    dtext(x0 + pad, top + pad, col(0x808890, 0.9),
      fit("No editable plug-in on the focused track", PW - 2 * pad, hf), hf)
    return
  end

  local rgb = (activeTab == "cs") and csRgb() or bcRgb()
  local _, fxname = reaper.TrackFX_GetFXName(tr, fx, "")
  dtext(x0 + pad, top + pad, col(0xC8CCD4, 1), fit(fxname or "", PW - 2 * pad, hf), hf)

  -- Filter line (type to filter, Backspace to delete — handled in loop()).
  local fy = top + pad + floor(19 * fs + 0.5)
  local ftxt = (paramFilter ~= "") and ("Filter: " .. paramFilter)
                                    or  "Type to filter\xE2\x80\xA6"
  dtext(x0 + pad, fy, col(0x8890A0, paramFilter ~= "" and 1 or 0.6),
    fit(ftxt, PW - 2 * pad, hf), hf)

  -- Active-domain mapped param NAMES (for the green-dot tint). hud_assign only
  -- carries names, so we match on name — exact enough for a visual hint.
  local mappedNames = {}
  for idx, a in pairs(asn) do
    local g = geom.ctrl[idx]
    if g and ((activeTab == "cs" and g.dom == "c")
           or (activeTab == "bc" and g.dom == "b")) then
      mappedNames[a.name] = true
    end
  end

  local params = getParams(tr, fx)
  local flo    = paramFilter:lower()
  local rows   = {}
  for _, pr in ipairs(params) do
    if flo == "" or pr.name:lower():find(flo, 1, true) then rows[#rows + 1] = pr end
  end

  local _, rowTH  = measure("Ag", rf)
  local lineH     = rowTH + floor(5 * fs + 0.5)
  local listTop   = fy + floor(20 * fs + 0.5)
  local listBot   = WH - pad
  paramMaxScroll  = math.max(0, #rows * lineH - (listBot - listTop))
  paramScroll     = clamp(paramScroll, 0, paramMaxScroll)

  local y = listTop - paramScroll
  for _, pr in ipairs(rows) do
    if y + lineH >= listTop and y <= listBot then
      local sel    = (pr.p == selectedParam)
      local mapped = mappedNames[pr.name]
      if sel then rect(x0 + 2, y - 1, PW - 4, lineH, col(rgb, 0.34)) end
      local tc = sel and 0xFFFFFF or (mapped and 0x78C898 or 0xC0C4CC)
      dtext(x0 + pad, y + 1, col(tc, 1), fit(pr.name, PW - 2 * pad - 12, rf), rf)
      if mapped then
        dtext(x0 + PW - pad - 8, y + 1, col(0x78C898, 1), "\xE2\x97\x8F", rf)
      end
      paramRects[#paramRects + 1] =
        { p = pr.p, name = pr.name, x = x0, y = y, w = PW, h = lineH }
    end
    y = y + lineH
  end
end

-- UF8 device tab — interactive strip-grid. 8 strip columns × 5 control rows
-- (V-Pot / Fader / Solo / Cut / Sel), left gutter carries the row labels, top
-- row the strip numbers. Cells show the bound plug-in param name (em-dash when
-- unmapped, "i" when inverted). Follows the live hardware banks via the state
-- header. Phase 2 (interactive): left-click a cell to learn (wiggle a param);
-- right-click for Invert / Fill sequential / Unbind; virgin plug-ins bootstrap
-- a UF8-only map. See handleUf8CellClick / drawUf8ControlContextMenu.
local UF8_KINDS = {
  { k = 0, l = "V-Pot" }, { k = 1, l = "Fader" }, { k = 2, l = "Solo" },
  { k = 3, l = "Cut"   }, { k = 4, l = "Sel"   },
}
local UF8_ACCENT = 0x4A90D8

local function renderUf8Grid(ust, uasn)
  ctrlRects = {}                                  -- no UC1 hit-rects on this tab
  uf8Rects  = {}
  local availW = WW - RW
  local top    = TAB_H
  local hpx    = floor(13 * fontScale() + 0.5)

  local sub
  if ust.present then
    sub = (ust.short ~= "" and ust.short or "UF8 plug-in")
        .. "      Fader Bank " .. (ust.faderBank + 1) .. " / 2"
  elseif ust.boot then
    sub = "Map " .. (ust.short ~= "" and ust.short or "plug-in")
        .. "  \xE2\x80\x94  click a cell, then wiggle a parameter to create a UF8 map"
  else
    sub = "No UF8 plug-in on the focused track"
  end
  local _, subH = measure(sub, hpx)
  dtext(10, top + 6, col(0x9098A4, 0.95), sub, hpx)
  -- Nothing to map (no UF8 map AND no virgin FX) → no grid.
  if not ust.present and not ust.boot then return end

  -- V-Pot bank selector row: the 8 Top-Soft-Key banks, current one bright.
  -- Clickable — click a bank to switch it (sets the live Top-Soft-Key bank via
  -- the extension), so you can reach + map empty banks from the HUD without
  -- needing the hardware soft-keys. The hardware Top-Soft-Keys drive it too.
  uf8BankRects = {}
  uf8Banks, uf8BankCols = readUf8Banks()
  local bankPx = floor(11 * fontScale() + 0.5)
  local bankY  = top + 6 + subH + 6
  local bankH  = floor(bankPx + 8 * fontScale() + 0.5)
  do
    local _, glh = measure("V-Pot Bank", bankPx)
    dtext(10, bankY + (bankH - glh) / 2, col(0x8890A0, 0.85), "V-Pot Bank", bankPx)
    local minW = floor(22 * fontScale() + 0.5)
    local maxW = floor(96 * fontScale() + 0.5)
    local bx   = 10 + measure("V-Pot Bank", bankPx) + 10
    -- Named banks show their label (right-click to rename); else the number.
    -- Cells auto-size to the label so names stay legible.
    for b = 0, 7 do
      local lbl  = uf8Banks[b]
      local txt  = (lbl and lbl ~= "") and lbl or tostring(b + 1)
      local tw   = measure(txt, bankPx)
      local cw   = math.max(minW, math.min(maxW, tw + 12))
      local disp = fit(txt, cw - 8, bankPx)
      local on   = (b == ust.vpotBank)
      rect(bx, bankY, cw, bankH, col(on and UF8_ACCENT or 0x2A2A30, on and 0.9 or 1))
      local dw, dh = measure(disp, bankPx)
      dtext(bx + (cw - dw) / 2, bankY + (bankH - dh) / 2,
            col(on and 0x121214 or 0x9098A4, on and 1 or 0.8), disp, bankPx)
      uf8BankRects[#uf8BankRects + 1] = { b = b, x = bx, y = bankY, w = cw, h = bankH }
      bx = bx + cw + 4
    end
  end

  local gridTop = bankY + bankH + 10
  local px      = floor(12 * fontScale() + 0.5)
  local lblPx   = floor(11 * fontScale() + 0.5)
  local rowH    = floor(px + 15 * fontScale() + 0.5)
  local Lw      = floor(58 * fontScale() + 0.5)
  local colW    = (availW - Lw - 6) / 8

  -- header row: strip numbers 1..8
  for s = 0, 7 do
    local cx = Lw + s * colW
    local hs = tostring(s + 1)
    local tw, th = measure(hs, lblPx)
    dtext(cx + (colW - tw) / 2, gridTop + (rowH - th) / 2,
          col(UF8_ACCENT, 0.9), hs, lblPx)
  end
  rect(0, gridTop + rowH - 1, availW, 1, col(0x303036, 1))

  for r, kd in ipairs(UF8_KINDS) do
    local ry = gridTop + r * rowH
    if r % 2 == 0 then rect(0, ry, availW, rowH, col(0xFFFFFF, 0.025)) end
    local _, lh = measure(kd.l, lblPx)
    dtext(6, ry + (rowH - lh) / 2, col(0xC0C6D0, 0.85), kd.l, lblPx)
    for s = 0, 7 do
      local cx = Lw + s * colW
      if s > 0 then rect(cx, ry, 1, rowH, col(0x2A2A30, 0.8)) end

      -- Armed-learn pulse on the cell.
      if uf8Learn == (kd.k * 8 + s) then
        local pulse = 0.22 + 0.22 * math.abs((frame % 50) / 25 - 1)
        rect(cx + 1, ry + 1, colW - 2, rowH - 2, col(0xFFFFFF, pulse))
      end

      local a = uasn[s] and uasn[s][kd.k]
      if a and a.name ~= "" then
        local nm    = fit(a.name, colW - (a.inv and 18 or 10), px)
        local _, nh = measure(nm, px)
        dtext(cx + 6, ry + (rowH - nh) / 2, col(0xE8ECF2, 0.96), nm, px)
        if a.inv then
          local _, ih = measure("i", lblPx)
          dtext(cx + colW - 11, ry + (rowH - ih) / 2, col(UF8_ACCENT, 0.9), "i", lblPx)
        end
      else
        local em = "\xE2\x80\x94"
        local _, eh = measure(em, px)
        dtext(cx + 6, ry + (rowH - eh) / 2, col(0x60656E, 0.7), em, px)
      end

      uf8Rects[#uf8Rects + 1] =
        { kind = kd.k, strip = s, x = cx, y = ry, w = colW, h = rowH }
    end
  end

  -- Learn banner (transient; UF8 has no param panel / Touch-to-Learn here). The
  -- boot hint lives in the subheader so it doesn't permanently cover anything.
  if uf8Learn >= 0 then
    local kd  = UF8_KINDS[floor(uf8Learn / 8) + 1]
    local stp = (uf8Learn % 8) + 1
    local msg = "Learning " .. ((kd and kd.l) or "?") .. " " .. stp
             .. "  \xE2\x80\x94  wiggle a plug-in parameter to bind it"
             .. "   (click again or Esc to cancel)"
    local mpx = floor(13 * fontScale() + 0.5)
    local tw, th = measure(msg, mpx)
    local bx, by = 8, top + 4
    rect(bx, by, tw + 20, th + 8, col(0x101418, 0.9))
    dtext(bx + 10, by + 4, col(0xFFD060, 1), msg, mpx)
  end
end

-- UF8 device tab — hardware MOCKUP view, faithful to the FX-Learn UF8 face
-- (drawUf8Face_): design-space 860x520 scaled to fit. The 8 Top-Soft-Keys are
-- the V-Pot bank selectors (label + colour); per strip a scribble LCD, a small
-- V-Pot ring, stacked SOLO/CUT/SEL and a tall fader. Rebuilds the same
-- uf8Rects/uf8BankRects the grid does so learn / colour / bank / context menus
-- work unchanged. The strip-grid (renderUf8Grid) is the list view.
local function renderUf8Face(ust, uasn)
  ctrlRects    = {}
  uf8Rects     = {}
  uf8BankRects = {}
  local availW = WW - RW
  local top    = TAB_H
  local hpx    = floor(13 * fontScale() + 0.5)

  local sub
  if ust.present then
    sub = (ust.short ~= "" and ust.short or "UF8 plug-in")
        .. "      Fader Bank " .. (ust.faderBank + 1) .. " / 2"
  elseif ust.boot then
    sub = "Map " .. (ust.short ~= "" and ust.short or "plug-in")
        .. "  \xE2\x80\x94  click a control, then wiggle a parameter to create a UF8 map"
  else
    sub = "No UF8 plug-in on the focused track"
  end
  local _, subH = measure(sub, hpx)
  dtext(10, top + 6, col(0x9098A4, 0.95), sub, hpx)
  uf8Banks, uf8BankCols = readUf8Banks()
  if not ust.present and not ust.boot then return end

  -- Design-space face (860x520) scaled to fill width, clamped to height.
  local FW, FH  = 860, 520
  local faceTop = top + 6 + subH + 8
  local availH  = WH - faceTop - 6
  if availH < 120 then return end
  local sc    = math.min((availW - 8) / FW, availH / FH)
  local faceX = math.max(4, (availW - FW * sc) / 2)

  local function px(dx) return faceX + dx * sc end
  local function py(dy) return faceTop + dy * sc end
  local function sr(dx, dy, dw, dh, fill, outl, rnd)
    local x1, y1 = OX + px(dx), OY + py(dy)
    local x2, y2 = OX + px(dx + dw), OY + py(dy + dh)
    reaper.ImGui_DrawList_AddRectFilled(dl, x1, y1, x2, y2, fill, (rnd or 0) * sc)
    if outl then
      reaper.ImGui_DrawList_AddRect(dl, x1, y1, x2, y2, outl, (rnd or 0) * sc, 0, math.max(1, sc))
    end
  end
  local function sctext(dxc, dyc, c, s, pxsz)
    local tw, th = measure(s, pxsz)
    dtext(px(dxc) - tw / 2, py(dyc) - th / 2, c, s, pxsz)
  end

  -- design constants (verbatim from SettingsScreen.cpp drawUf8Face_)
  local STRIPW, GAP, OXs = 80, 7, 86
  local function cxOf(s) return OXs + s * (STRIPW + GAP) + STRIPW / 2 end
  local TSK_Y, TSK_H = 12, 22
  local SCR_Y, SCR_H = 40, 58
  local BAR_H        = 16
  local VP_CY, VP_R  = 124, 18
  local SOLO_Y, CUT_Y, SEL_Y, SCS_H = 152, 172, 192, 16
  local RAIL_Y, RAIL_H, RAIL_W = 216, 240, 22

  local fLbl = math.max(7, floor(12 * sc + 0.5))
  local fSm  = math.max(7, floor(11 * sc + 0.5))

  local cChassis = col(0x14181E, 1)
  local cEdge    = col(0x2A3038, 1)
  local cBtnFill = col(0x252A33, 1)
  local cBtnEdge = col(0x4A5060, 1)
  local cScrib   = col(0x080C12, 1)
  local cScrEdge = col(0x444A55, 1)
  local cRingIn  = col(0x555A66, 1)
  local cRail    = col(0x303338, 1)
  local cSilk    = col(0x9CA0AA, 1)
  local cAcc     = col(UF8_ACCENT, 1)
  local cAccDim  = col(UF8_ACCENT, 0.28)
  local cWhite12 = col(0x121214, 1)

  sr(4, 4, FW - 8, FH - 8, cChassis, cEdge, 8)

  local vpotBank = ust.vpotBank or 0
  for s = 0, 7 do
    local cx   = cxOf(s)
    local colX = cx - STRIPW / 2

    -- Top-Soft-Key = bank-s selector (label + colour). Active = current bank.
    do
      local on   = (s == vpotBank)
      local bcol = uf8BankCols[s]
      sr(colX + 6, TSK_Y, STRIPW - 12, TSK_H, on and cAcc or cBtnFill, cBtnEdge, 3.5)
      if bcol then
        sr(colX + 6, TSK_Y + TSK_H - 3, STRIPW - 12, 3, (bcol << 8) | 0xFF, nil, 0)
      end
      local lbl = uf8Banks[s]
      lbl = (lbl and lbl ~= "") and lbl or tostring(s + 1)
      sctext(cx, TSK_Y + TSK_H / 2 - 1, on and cWhite12 or cSilk,
             fit(lbl, (STRIPW - 16) * sc, fLbl), fLbl)
      uf8BankRects[#uf8BankRects + 1] =
        { b = s, x = px(colX + 6), y = py(TSK_Y), w = (STRIPW - 12) * sc, h = TSK_H * sc }
    end

    -- Scribble LCD — V-Pot param name (mapped) like a track name.
    sr(colX + 4, SCR_Y, STRIPW - 8, SCR_H, cScrib, cScrEdge, 2)
    do
      local a = uasn[s] and uasn[s][0]
      if a and a.name ~= "" then
        sctext(cx, SCR_Y + 14, col(0xE8ECF2, 0.96), fit(a.name, (STRIPW - 12) * sc, fSm), fSm)
      end
    end
    -- colour bar (per-strip stripColour not in the HUD payload yet → neutral).
    sr(colX + 6, SCR_Y + SCR_H - BAR_H - 2, STRIPW - 12, BAR_H, col(0x404040, 0.5), nil, 0)

    -- V-Pot ring (kind 0)
    do
      local a        = uasn[s] and uasn[s][0]
      local mapped   = a and a.name ~= ""
      local learning = uf8Learn == s
      local scx, scy = OX + px(cx), OY + py(VP_CY)
      local r = VP_R * sc
      reaper.ImGui_DrawList_AddCircleFilled(dl, scx, scy, r, cChassis)
      reaper.ImGui_DrawList_AddCircle(dl, scx, scy, r, cBtnEdge, 0, math.max(1, sc))
      reaper.ImGui_DrawList_AddCircleFilled(dl, scx, scy, r - 4 * sc, mapped and cAcc or cBtnFill)
      reaper.ImGui_DrawList_AddCircle(dl, scx, scy, r - 4 * sc, cRingIn, 0, math.max(1, sc))
      if learning then
        local p = 0.4 + 0.4 * math.abs((frame % 50) / 25 - 1)
        reaper.ImGui_DrawList_AddCircle(dl, scx, scy, r + 2 * sc, col(0xFFFFFF, p), 0, math.max(1, 2 * sc))
      end
      reaper.ImGui_DrawList_AddLine(dl, scx, scy - (VP_R - 2) * sc, scx, scy - (VP_R - 10) * sc,
        col(0xCCCCCC, 1), math.max(1, 2 * sc))
      uf8Rects[#uf8Rects + 1] =
        { kind = 0, strip = s, x = px(cx - VP_R), y = py(VP_CY - VP_R), w = 2 * VP_R * sc, h = 2 * VP_R * sc }
    end

    -- SOLO / CUT / SEL stacked (kinds 2/3/4)
    local function scsBtn(kind, dy, deflbl)
      local a        = uasn[s] and uasn[s][kind]
      local mapped   = a and a.name ~= ""
      local learning = uf8Learn == (kind * 8 + s)
      sr(colX + 8, dy, STRIPW - 16, SCS_H, mapped and cAccDim or cBtnFill,
         mapped and cAcc or cBtnEdge, 3)
      if learning then
        local p = 0.3 + 0.3 * math.abs((frame % 50) / 25 - 1)
        sr(colX + 7, dy - 1, STRIPW - 14, SCS_H + 2, 0, col(0xFFFFFF, p), 3)
      end
      sctext(cx, dy + SCS_H / 2, mapped and col(0xFFFFFF, 1) or cSilk,
             fit(mapped and a.name or deflbl, (STRIPW - 18) * sc, fSm), fSm)
      uf8Rects[#uf8Rects + 1] =
        { kind = kind, strip = s, x = px(colX + 8), y = py(dy), w = (STRIPW - 16) * sc, h = SCS_H * sc }
    end
    scsBtn(2, SOLO_Y, "SOLO")
    scsBtn(3, CUT_Y,  "CUT")
    scsBtn(4, SEL_Y,  "SEL")

    -- Fader rail + thumb (kind 1)
    do
      local a        = uasn[s] and uasn[s][1]
      local mapped   = a and a.name ~= ""
      local learning = uf8Learn == (8 + s)
      sr(cx - RAIL_W / 2, RAIL_Y, RAIL_W, RAIL_H, cRail, cEdge, 4)
      local thumbY = RAIL_Y + RAIL_H / 2 - 5
      sr(cx - RAIL_W / 2 - 1, thumbY, RAIL_W + 2, 10, mapped and cAcc or col(0x60C060, 0.85),
         col(0xE8F0E8, 1), 2)
      if learning then
        local p = 0.3 + 0.3 * math.abs((frame % 50) / 25 - 1)
        sr(cx - RAIL_W / 2 - 2, thumbY - 1, RAIL_W + 4, 12, 0, col(0xFFFFFF, p), 2)
      end
      if mapped then
        sctext(cx, RAIL_Y - 8, col(0xC0C6D0, 0.95), fit(a.name, STRIPW * sc, fSm), fSm)
      end
      uf8Rects[#uf8Rects + 1] =
        { kind = 1, strip = s, x = px(cx - RAIL_W / 2 - 4), y = py(RAIL_Y),
          w = (RAIL_W + 8) * sc, h = RAIL_H * sc }
    end

    sctext(cx, RAIL_Y + RAIL_H + 14, col(0x707680, 1), tostring(s + 1), fSm)
  end

  -- Bank L / R (decorative; hardware Bank buttons drive the live fader bank).
  do
    local bcx = (cxOf(3) + cxOf(4)) / 2
    local BW, BY, BH = 60, 482, 18
    sr(bcx - 4 - BW, BY, BW, BH, cBtnFill, cBtnEdge, 3)
    sctext(bcx - 4 - BW / 2, BY + BH / 2, cSilk, "BANK \xE2\x97\x82", fSm)
    sr(bcx + 4, BY, BW, BH, cBtnFill, cBtnEdge, 3)
    sctext(bcx + 4 + BW / 2, BY + BH / 2, cSilk, "BANK \xE2\x96\xB8", fSm)
  end

  if uf8Learn >= 0 then
    local kd  = UF8_KINDS[floor(uf8Learn / 8) + 1]
    local stp = (uf8Learn % 8) + 1
    local msg = "Learning " .. ((kd and kd.l) or "?") .. " " .. stp
             .. "  \xE2\x80\x94  wiggle a plug-in parameter to bind it"
             .. "   (click again or Esc to cancel)"
    local mpx = floor(13 * fontScale() + 0.5)
    local tw, th = measure(msg, mpx)
    rect(8, top + 4, tw + 20, th + 8, col(0x101418, 0.9))
    dtext(18, top + 8, col(0xFFD060, 1), msg, mpx)
  end
end

local function render()
  TAB_H = 38   -- fixed chrome height (text-size option only affects list/param body)

  local st   = readState()
  local asn  = readAssign()
  local ust  = readUf8State()
  local uasn = readUf8Assign()

  -- Param-list drawer: reserve a fixed right strip. The window itself grows by
  -- PARAM_PW when the drawer opens (loop()), so the mockup keeps its size.
  paramPanelOpen = (reaper.GetExtState(SECT, "hud_imgui_params") == "1")
  if paramPanelOpen then
    RW = PARAM_PW
  else
    RW, paramRects, selectedParam = 0, {}, -1
  end

  frame = frame + 1
  local hl = reaper.GetExtState(SECT, "hud_learn")
  learnIdx = (hl ~= "" and tonumber(hl)) or -1
  local ul = reaper.GetExtState(SECT, "hud_uf8_learn")
  uf8Learn = (ul ~= "" and tonumber(ul)) or -1
  local hint = reaper.GetExtState(SECT, "hud_hint")
  if hint ~= "" then hintText = hint; hintFrames = 90
    reaper.SetExtState(SECT, "hud_hint", "", false) end

  -- Bootstrap: surface is pointed at an unlearned FX ("1;<name>") → the tabs are
  -- published empty + the LCD shows <name>; surface a "Map <name>" hint.
  local bootRaw    = reaper.GetExtState(SECT, "hud_boot")
  local bootActive = (bootRaw:sub(1, 2) == "1;")
  local bootName   = bootActive and bootRaw:sub(3) or ""

  -- Auto-follow focus to the matching tab. CS/BC win (focusDom c/b); otherwise a
  -- UF8-mapped plug-in under the cursor (ust.focus) switches to the UF8 tab.
  -- Manual tab clicks stick until the focus context next CHANGES (lastFocusDom
  -- doubles as the focus-key tracker: "c"/"b"/"u"/"n").
  local focusKey = st.focusDom
  if focusKey ~= "c" and focusKey ~= "b" and ust and ust.focus then focusKey = "u" end
  if focusKey ~= lastFocusDom then
    local newTab = (focusKey == "c") and "cs"
               or  (focusKey == "b") and "bc"
               or  (focusKey == "u") and "uf8" or nil
    if newTab and newTab ~= activeTab then
      selectedParam = -1   -- param idx is per-FX
      activeTab = newTab
    end
    lastFocusDom = focusKey
  end

  drawTabs(st, ust)

  -- Tell the extension when the UF8 tab is showing so it auto-engages UF8
  -- Plugin Mode (hardware Top-Soft-Keys drive V-Pot banks). Edge-write only.
  local onUf8 = (activeTab == "uf8")
  if lastUf8Tab ~= onUf8 then
    lastUf8Tab = onUf8
    reaper.SetExtState(SECT, "hud_uf8_tab", onUf8 and "1" or "0", false)
  end

  -- UF8 device tab: list view = strip-grid, mockup view = hardware face. Mirrors
  -- the CS/BC list/mockup toggle (shared hud_imgui_view). Independent of the UC1
  -- geometry.
  if onUf8 then
    if reaper.GetExtState(SECT, "hud_imgui_view") == "mockup" then
      renderUf8Face(ust, uasn)
    else
      renderUf8Grid(ust, uasn)
    end
    return
  end

  local nCtrl = 0; for _ in pairs(geom.ctrl) do nCtrl = nCtrl + 1 end

  if not geom or nCtrl == 0 then
    dtext(10, TAB_H + 12, col(0x808890, 0.9),
      "Waiting for surface geometry\xE2\x80\xA6", floor(15 * fontScale() + 0.5))
    return
  end

  local present = (activeTab == "cs") and st.csPresent or st.bcPresent

  if reaper.GetExtState(SECT, "hud_imgui_view") == "mockup" then
    -- Unified face draws the whole surface (both domains, inactive dimmed) —
    -- always shown; absent plug-ins just render greyed, no "no plug-in" gate.
    renderFace(st, asn)
  else
    -- Always render the list — even with no plug-in the rows draw as em-dash
    -- and stay click-armable, so an EMPTY tab can bootstrap a virgin plug-in:
    -- click a control → wiggle its param → the extension creates a new map.
    renderList(st, asn)
  end

  if paramPanelOpen then renderParamPanel(st, asn) end

  local function banner(msg, bgRgb, bgA, fgRgb)
    local px = floor(14 * fontScale() + 0.5)
    local tw, th = measure(msg, px)
    local bw, bh = tw + 20, th + 8
    local bx, by = (WW - RW - bw) / 2, TAB_H + 4
    rect(bx, by, bw, bh, col(bgRgb, bgA))
    dtext(bx + 10, by + 4, col(fgRgb, 1), msg, px)
  end

  if selectedParam >= 0 then
    banner("Assigning " .. selectedParamNm ..
           "  \xE2\x80\x94  click a control to bind it   (Esc to cancel)",
           0x10202C, 0.9, 0x70C0FF)
  elseif learnIdx < 0 and reaper.GetExtState(SECT, "hud_touch_learn") == "1" then
    banner("Touch to Learn  \xE2\x80\x94  move a UC1 control to arm it, "
           .. "then wiggle a plug-in parameter", 0x101A14, 0.88, 0x70D0A0)
  elseif learnIdx >= 0 then
    local c   = geom.ctrl[learnIdx]
    local lbl = (c and c.label ~= "" and c.label) or ("#" .. learnIdx)
    banner("Learning " .. lbl ..
           "  \xE2\x80\x94  wiggle a plug-in parameter to bind it"
           .. "   (click again or Esc to cancel)",
           0x101418, 0.88, 0xFFD060)
  elseif hintFrames > 0 then
    hintFrames = hintFrames - 1
    banner(hintText, 0x301014, 0.92, 0xFF8888)
  elseif bootActive then
    banner("Map " .. bootName .. "  \xE2\x80\x94  click a control, then wiggle "
           .. "its parameter (binds to " .. (activeTab == "cs" and "Channel Strip"
           or "Bus Comp") .. ")", 0x10201A, 0.90, 0x84E0A8)
  elseif not present then
    banner((activeTab == "cs" and "No Channel Strip" or "No Bus Comp")
           .. " plug-in  \xE2\x80\x94  click a control, then wiggle a parameter "
           .. "to create a map", 0x101A20, 0.88, 0x80B8E0)
  end
end

------------------------------------------------------------------------
-- Right-click menu (ImGui popup). Dock is handled by ReaImGui's built-in
-- title-bar context menu, so it's no longer in here.
------------------------------------------------------------------------
local POPUP          = "##hud_ctx"
local CTRL_POPUP     = "##hud_ctrl_ctx"
local UF8_CTRL_POPUP = "##hud_uf8_ctrl_ctx"
local UF8_BANK_POPUP = "##hud_uf8_bank_ctx"

-- List / parameter-panel body text size (Medium = current default). Chrome
-- (titles, badge, buttons) is unaffected.
local FONT_PRESETS = {
  { l = "XS",     v = 0.75 },
  { l = "S",      v = 0.88 },
  { l = "Medium", v = 1.0  },
  { l = "L",      v = 1.2  },
  { l = "XL",     v = 1.45 },
}

local function drawContextMenu()
  -- Roomier popup — WindowPadding read at BeginPopup, so push first; pop always.
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowPadding(), 10, 8)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_ItemSpacing(), 10, 7)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_FramePadding(), 7, 4)
  if not reaper.ImGui_BeginPopup(ctx, POPUP) then
    reaper.ImGui_PopStyleVar(ctx, 3)
    return
  end

  local mockup = (reaper.GetExtState(SECT, "hud_imgui_view") == "mockup")
  if reaper.ImGui_BeginMenu(ctx, "View") then
    if reaper.ImGui_MenuItem(ctx, "List", nil, not mockup) then
      reaper.SetExtState(SECT, "hud_imgui_view", "list", true)
    end
    if reaper.ImGui_MenuItem(ctx, "Mockup", nil, mockup) then
      reaper.SetExtState(SECT, "hud_imgui_view", "mockup", true)
    end
    reaper.ImGui_EndMenu(ctx)
  end

  local pPanel = (reaper.GetExtState(SECT, "hud_imgui_params") == "1")
  if reaper.ImGui_MenuItem(ctx, "Parameter List", nil, pPanel) then
    reaper.SetExtState(SECT, "hud_imgui_params", pPanel and "0" or "1", true)
  end

  -- Touch-to-Learn: move a UC1 control to arm it (instead of clicking the
  -- mockup). Session-only (persist=false) so it never leaves the surface inert
  -- across restarts; also cleared on HUD shutdown.
  local touchLearn = (reaper.GetExtState(SECT, "hud_touch_learn") == "1")
  if reaper.ImGui_MenuItem(ctx, "Touch to Learn", nil, touchLearn) then
    reaper.SetExtState(SECT, "hud_touch_learn", touchLearn and "0" or "1", false)
  end

  if reaper.ImGui_BeginMenu(ctx, "Text size (list)") then
    local fs = fontScale()
    for _, p in ipairs(FONT_PRESETS) do
      if reaper.ImGui_MenuItem(ctx, p.l, nil, math.abs(fs - p.v) < 0.01) then
        reaper.SetExtState(SECT, "hud_imgui_font", tostring(p.v), true)
      end
    end
    reaper.ImGui_EndMenu(ctx)
  end

  local white = (reaper.GetExtState(SECT, "hud_text_white") == "1")
  if reaper.ImGui_BeginMenu(ctx, "Text colour") then
    if reaper.ImGui_MenuItem(ctx, "CS / BC colour", nil, not white) then
      reaper.SetExtState(SECT, "hud_text_white", "0", true)
    end
    if reaper.ImGui_MenuItem(ctx, "White", nil, white) then
      reaper.SetExtState(SECT, "hud_text_white", "1", true)
    end
    reaper.ImGui_EndMenu(ctx)
  end

  reaper.ImGui_Separator(ctx)
  if reaper.ImGui_MenuItem(ctx, "Close HUD") then
    reaper.SetExtState(SECT, RUNKEY, "0", false)
  end
  reaper.ImGui_EndPopup(ctx)
  reaper.ImGui_PopStyleVar(ctx, 3)   -- matches the 3 pushes before BeginPopup
end

-- Per-control context menu (right-click a control): Learn / Invert / Unbind.
-- Acts on ctxCtrlIdx (captured at right-click) via the hud_cmd channel; the
-- extension resolves the active modifier layer + domain target. Invert/Unbind
-- are disabled on an unmapped control (nothing to act on). State (mapped/inv) is
-- read fresh from hud_assign so the tick + name reflect the current binding.
local function drawControlContextMenu()
  if ctxCtrlIdx < 0 then return end
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowPadding(), 10, 8)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_ItemSpacing(), 10, 7)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_FramePadding(), 7, 4)
  if not reaper.ImGui_BeginPopup(ctx, CTRL_POPUP) then
    reaper.ImGui_PopStyleVar(ctx, 3)
    return
  end

  local asn    = readAssign()
  local a      = asn[ctxCtrlIdx]
  local mapped = a ~= nil
  local g      = geom.ctrl[ctxCtrlIdx]

  -- Header: SSL slot label + bound param name (disabled, informational).
  reaper.ImGui_BeginDisabled(ctx)
  local hdr = (g and g.label) or ("Control " .. ctxCtrlIdx)
  if mapped then hdr = hdr .. "  \xE2\x86\x92  " .. a.name end
  reaper.ImGui_MenuItem(ctx, hdr)
  reaper.ImGui_EndDisabled(ctx)
  reaper.ImGui_Separator(ctx)

  if reaper.ImGui_MenuItem(ctx, "Learn (wiggle a parameter)") then
    sendCmd("learn;" .. ctxCtrlIdx)
  end
  if not mapped then reaper.ImGui_BeginDisabled(ctx) end
  if reaper.ImGui_MenuItem(ctx, "Invert", nil, mapped and a.inv or false) then
    sendCmd("invert;" .. ctxCtrlIdx .. ";" .. ctxCtrlLayer)
  end
  if reaper.ImGui_MenuItem(ctx, "Rename\xE2\x80\xA6") then
    -- Native modal (works from a defer/ImGui frame, same as the gfx panel menus).
    -- Strip commas from the prefill (GetUserInputs splits the default on commas)
    -- and the ';'/newline hud_cmd delimiters from the result.
    local cur = ((mapped and a.name) or ""):gsub(",", " ")
    local ok, val = reaper.GetUserInputs("Rename control", 1,
      "Display name (empty = default):,extrawidth=180", cur)
    if ok then
      sendCmd("rename;" .. ctxCtrlIdx .. ";" .. ctxCtrlLayer .. ";"
              .. (val:gsub("[;\n]", " ")))
    end
  end
  if reaper.ImGui_MenuItem(ctx, "Unbind") then
    sendCmd("unbind;" .. ctxCtrlIdx .. ";" .. ctxCtrlLayer)
  end
  if not mapped then reaper.ImGui_EndDisabled(ctx) end

  reaper.ImGui_EndPopup(ctx)
  reaper.ImGui_PopStyleVar(ctx, 3)
end

-- UF8 grid cell right-click menu: Learn / Invert / Fill sequential / Unbind.
-- Acts on (ctxUf8Kind, ctxUf8Strip) at the live banks (resolved C++-side) via
-- the hud_cmd channel. UF8 has no modifier layers and no rename (the grid shows
-- the plug-in's own param name, not a per-control label).
local function drawUf8ControlContextMenu()
  if ctxUf8Kind < 0 then return end
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowPadding(), 10, 8)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_ItemSpacing(), 10, 7)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_FramePadding(), 7, 4)
  if not reaper.ImGui_BeginPopup(ctx, UF8_CTRL_POPUP) then
    reaper.ImGui_PopStyleVar(ctx, 3)
    return
  end

  local uasn   = readUf8Assign()
  local a      = uasn[ctxUf8Strip] and uasn[ctxUf8Strip][ctxUf8Kind]
  local mapped = (a ~= nil and a.name ~= "")
  local kd     = UF8_KINDS[ctxUf8Kind + 1]

  -- Header (disabled, informational): "<kind> <strip>  →  <param>".
  reaper.ImGui_BeginDisabled(ctx)
  local hdr = ((kd and kd.l) or "Cell") .. " " .. (ctxUf8Strip + 1)
  if mapped then hdr = hdr .. "  \xE2\x86\x92  " .. a.name end
  reaper.ImGui_MenuItem(ctx, hdr)
  reaper.ImGui_EndDisabled(ctx)
  reaper.ImGui_Separator(ctx)

  if reaper.ImGui_MenuItem(ctx, "Learn (wiggle a parameter)") then
    sendCmd("uf8learn;" .. ctxUf8Kind .. ";" .. ctxUf8Strip)
  end
  if not mapped then reaper.ImGui_BeginDisabled(ctx) end
  if reaper.ImGui_MenuItem(ctx, "Invert", nil, mapped and a.inv or false) then
    sendCmd("uf8invert;" .. ctxUf8Kind .. ";" .. ctxUf8Strip)
  end
  -- V-Pot only: Value (continuous) vs Toggle (push = on/off).
  if ctxUf8Kind == 0 then
    if reaper.ImGui_BeginMenu(ctx, "V-Pot mode") then
      if reaper.ImGui_MenuItem(ctx, "Value (continuous)", nil, a and a.mode == 0) then
        sendCmd("uf8vmode;" .. ctxUf8Strip .. ";0")
      end
      if reaper.ImGui_MenuItem(ctx, "Toggle (push on/off)", nil, a and a.mode == 1) then
        sendCmd("uf8vmode;" .. ctxUf8Strip .. ";1")
      end
      reaper.ImGui_EndMenu(ctx)
    end
  end
  -- Fill sequential: only for strips 1..7 (binds strips to the right).
  if ctxUf8Strip >= 7 then reaper.ImGui_BeginDisabled(ctx) end
  if reaper.ImGui_MenuItem(ctx, "Fill sequential \xE2\x86\x92") then
    sendCmd("uf8fill;" .. ctxUf8Kind .. ";" .. ctxUf8Strip)
  end
  if ctxUf8Strip >= 7 then reaper.ImGui_EndDisabled(ctx) end
  if reaper.ImGui_MenuItem(ctx, "Unbind") then
    sendCmd("uf8unbind;" .. ctxUf8Kind .. ";" .. ctxUf8Strip)
  end
  if not mapped then reaper.ImGui_EndDisabled(ctx) end

  reaper.ImGui_EndPopup(ctx)
  reaper.ImGui_PopStyleVar(ctx, 3)
end

-- V-Pot bank right-click menu: rename / clear the bank's display name (the
-- hardware Top-Soft-Key label). Acts on ctxUf8Bank via the hud_cmd channel.
local function drawUf8BankContextMenu()
  if ctxUf8Bank < 0 then return end
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowPadding(), 10, 8)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_ItemSpacing(), 10, 7)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_FramePadding(), 7, 4)
  if not reaper.ImGui_BeginPopup(ctx, UF8_BANK_POPUP) then
    reaper.ImGui_PopStyleVar(ctx, 3)
    return
  end

  local cur = uf8Banks[ctxUf8Bank] or ""

  reaper.ImGui_BeginDisabled(ctx)
  local hdr = "V-Pot Bank " .. (ctxUf8Bank + 1)
  if cur ~= "" then hdr = hdr .. "  \xE2\x86\x92  " .. cur end
  reaper.ImGui_MenuItem(ctx, hdr)
  reaper.ImGui_EndDisabled(ctx)
  reaper.ImGui_Separator(ctx)

  if reaper.ImGui_MenuItem(ctx, "Rename\xE2\x80\xA6") then
    local prefill = cur:gsub(",", " ")
    local ok, val = reaper.GetUserInputs("Rename V-Pot bank", 1,
      "Bank name (empty = number):,extrawidth=160", prefill)
    if ok then
      sendCmd("uf8bankname;" .. ctxUf8Bank .. ";" .. (val:gsub("[;\n]", " ")))
    end
  end
  if cur == "" then reaper.ImGui_BeginDisabled(ctx) end
  if reaper.ImGui_MenuItem(ctx, "Clear name") then
    sendCmd("uf8bankname;" .. ctxUf8Bank .. ";")
  end
  if cur == "" then reaper.ImGui_EndDisabled(ctx) end

  -- Colour picker — identical packing + flags to the FX-Learn palette
  -- (ColorButton takes 0xRRGGBBAA, flags 0; the earlier NoAlpha/NoBorder flags
  -- were the only difference and made the swatches read swapped). A separate
  -- swatch shows the current colour; the 10 SSL DAW-Colour swatches set it.
  reaper.ImGui_Separator(ctx)
  local curCol = uf8BankCols[ctxUf8Bank]
  reaper.ImGui_Text(ctx, "Colour")
  reaper.ImGui_SameLine(ctx)
  reaper.ImGui_ColorButton(ctx, "##bankcolcur",
    curCol and ((curCol << 8) | 0xFF) or 0x40404080, 0, 44, 18)
  for i, rgb in ipairs(UF8_BANK_PALETTE) do
    if reaper.ImGui_ColorButton(ctx, "##bankcol" .. i, (rgb << 8) | 0xFF, 0, 20, 20) then
      sendCmd(string.format("uf8bankcolour;%d;%06X", ctxUf8Bank, rgb))
      reaper.ImGui_CloseCurrentPopup(ctx)
    end
    if i % 5 ~= 0 then reaper.ImGui_SameLine(ctx) end
  end

  reaper.ImGui_EndPopup(ctx)
  reaper.ImGui_PopStyleVar(ctx, 3)
end

------------------------------------------------------------------------
-- Geometry persistence (own keys → coexists with gfx HUD).
------------------------------------------------------------------------
local DEF_W, DEF_H = 920, 720
local function loadRect()
  local raw = reaper.GetExtState(SECT, "hud_imgui_rect")
  local x, y, w, h = raw:match("^(%-?%d+),(%-?%d+),(%d+),(%d+)$")
  if x then return tonumber(x), tonumber(y), tonumber(w), tonumber(h) end
  return nil
end
local restore_x, restore_y, restore_w, restore_h = loadRect()
local first_frame = true
local last_x, last_y, last_w, last_h
local lastParamPanelOpen = nil   -- edge-detect drawer toggle → grow/shrink window

------------------------------------------------------------------------
-- Main loop.
------------------------------------------------------------------------
local WFLAGS = reaper.ImGui_WindowFlags_NoScrollbar()
  | reaper.ImGui_WindowFlags_NoScrollWithMouse()

local shutdown
local function loop()
  if reaper.GetExtState(SECT, RUNKEY) ~= "1" then return shutdown() end

  if first_frame then
    if restore_x then
      reaper.ImGui_SetNextWindowPos(ctx, restore_x, restore_y)
      reaper.ImGui_SetNextWindowSize(ctx, restore_w, restore_h)
    else
      reaper.ImGui_SetNextWindowSize(ctx, DEF_W, DEF_H)
    end
    first_frame = false
  end

  -- Grow the window outward when the param drawer opens (and shrink back when it
  -- closes) so the mockup keeps its size instead of being squeezed. Edge-detect
  -- the toggle from any source (button or menu). On startup adopt the current
  -- state without resizing — the saved rect already matches it.
  local paramOpenNow = (reaper.GetExtState(SECT, "hud_imgui_params") == "1")
  if lastParamPanelOpen == nil then
    lastParamPanelOpen = paramOpenNow
  elseif paramOpenNow ~= lastParamPanelOpen then
    if last_w and last_h then
      local nw = paramOpenNow and (last_w + PARAM_PW)
                              or  math.max(200, last_w - PARAM_PW)
      reaper.ImGui_SetNextWindowSize(ctx, nw, last_h)
    end
    lastParamPanelOpen = paramOpenNow
  end

  -- Edge-to-edge content (we draw our own margins, like the gfx HUD).
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowPadding(), 0, 0)
  reaper.ImGui_PushStyleColor(ctx, reaper.ImGui_Col_WindowBg(), col(0x1F1F21, 1))

  local visible, open = reaper.ImGui_Begin(ctx, 'Rea-Sixty Learn-HUD###hud', true, WFLAGS)
  if visible then
    dl     = reaper.ImGui_GetWindowDrawList(ctx)
    OX, OY = reaper.ImGui_GetCursorScreenPos(ctx)
    WW, WH = reaper.ImGui_GetContentRegionAvail(ctx)

    -- Input — only when this window is hovered (popups capture otherwise).
    if reaper.ImGui_IsWindowHovered(ctx) then
      local mx, my = reaper.ImGui_GetMousePos(ctx)
      local lx, ly = mx - OX, my - OY
      if reaper.ImGui_IsMouseClicked(ctx, 1) and ly >= 0 then
        -- Content-area right-click only; the title bar keeps ReaImGui's own
        -- dock menu (negative ly = title bar, since OY is the content origin).
        if activeTab == "uf8" then
          -- Over a V-Pot bank → rename menu; over a grid cell → per-cell menu;
          -- else the main HUD menu.
          local bk = uf8BankAt(lx, ly)
          local k, s = uf8CellAt(lx, ly)
          if bk then
            ctxUf8Bank = bk
            reaper.ImGui_OpenPopup(ctx, UF8_BANK_POPUP)
          elseif k then
            ctxUf8Kind, ctxUf8Strip = k, s
            reaper.ImGui_OpenPopup(ctx, UF8_CTRL_POPUP)
          else
            reaper.ImGui_OpenPopup(ctx, POPUP)
          end
        else
          -- Over a control → per-control menu (Learn/Invert/Unbind); else the
          -- main HUD menu.
          local cidx = controlAt(lx, ly)
          if cidx then
            ctxCtrlIdx = cidx
            -- Latch the held-modifier layer NOW (right-click, modifier still
            -- down). readState() re-parses the live hud_state payload — `st`
            -- from render() isn't in scope in loop(). The native rename dialog
            -- later releases the modifier, so we must capture it here.
            ctxCtrlLayer = readState().layer
            reaper.ImGui_OpenPopup(ctx, CTRL_POPUP)
          else
            reaper.ImGui_OpenPopup(ctx, POPUP)
          end
        end
      elseif reaper.ImGui_IsMouseClicked(ctx, 0) then
        if activeTab == "uf8" then
          -- Tab strip → bank row → grid cell. A click that misses everything
          -- cancels any armed learn (an easy mouse "escape").
          if handleTabClick(lx, ly) then
          else
            local b = uf8BankAt(lx, ly)
            if b then sendCmd("uf8bank;" .. b)
            elseif not handleUf8CellClick(lx, ly) then
              if uf8Learn >= 0 then sendCmd("uf8cancel") end
            end
          end
        -- Top toggle buttons first, then param-panel row, then tab, then control.
        elseif handleLearnBtnClick(lx, ly) or handleParamBtnClick(lx, ly) then
        elseif paramPanelOpen and lx >= WW - RW and ly >= TAB_H then
          handleParamClick(lx, ly)
        elseif not handleTabClick(lx, ly) then
          handleControlClick(lx, ly)
        end
      end
      local wheel = reaper.ImGui_GetMouseWheel(ctx)
      if wheel ~= 0 then
        if paramPanelOpen and lx >= WW - RW then
          paramScroll = clamp(paramScroll - wheel * 40, 0, paramMaxScroll)
        else
          scrollY = scrollY - wheel * 40
        end
      end
    end

    -- Type-to-filter the param list (no InputText widget). Drains the ImGui
    -- char queue + Backspace while the drawer is open and the window focused.
    if paramPanelOpen and reaper.ImGui_IsWindowFocused(ctx)
       and reaper.ImGui_GetInputQueueCharacter then
      local i = 0
      while true do
        local ok, ch = reaper.ImGui_GetInputQueueCharacter(ctx, i)
        if not ok then break end
        if ch >= 32 and ch < 127 then paramFilter = paramFilter .. string.char(ch) end
        i = i + 1
      end
      if reaper.ImGui_IsKeyPressed(ctx, reaper.ImGui_Key_Backspace())
         and #paramFilter > 0 then
        paramFilter = paramFilter:sub(1, #paramFilter - 1)
      end
    end

    if reaper.ImGui_IsKeyPressed(ctx, reaper.ImGui_Key_Escape()) then
      if selectedParam >= 0 then selectedParam = -1
      elseif paramFilter ~= "" then paramFilter = ""
      elseif uf8Learn >= 0 then sendCmd("uf8cancel")
      elseif learnIdx >= 0 then sendCmd("cancel") end
    end

    refreshGeom()
    render()
    drawContextMenu()
    drawControlContextMenu()
    drawUf8ControlContextMenu()
    drawUf8BankContextMenu()

    -- Persist geometry on change.
    local px, py = reaper.ImGui_GetWindowPos(ctx)
    local pw, ph = reaper.ImGui_GetWindowSize(ctx)
    px, py, pw, ph = floor(px), floor(py), floor(pw), floor(ph)
    if px ~= last_x or py ~= last_y or pw ~= last_w or ph ~= last_h then
      last_x, last_y, last_w, last_h = px, py, pw, ph
      reaper.SetExtState(SECT, "hud_imgui_rect",
        string.format("%d,%d,%d,%d", px, py, pw, ph), true)
    end
  end
  reaper.ImGui_End(ctx)            -- UNCONDITIONAL (see reaimgui_v010_pairing_rules)
  reaper.ImGui_PopStyleColor(ctx, 1)
  reaper.ImGui_PopStyleVar(ctx, 1)

  if open then reaper.defer(loop) else shutdown() end
end

shutdown = function()
  reaper.SetExtState(SECT, RUNKEY, "0", false)
  reaper.SetExtState(SECT, "hud_touch_learn", "0", false)   -- don't leave UC1 inert
  reaper.SetExtState(SECT, "hud_uf8_tab", "0", false)       -- let C++ revert Plugin Mode
  setToggle(false)
end

reaper.atexit(shutdown)
reaper.defer(loop)
